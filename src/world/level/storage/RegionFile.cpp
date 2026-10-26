#include "RegionFile.h"
#include "../../../platform/log.h"
#include <climits>

const int SECTOR_BYTES = 4096;
const int SECTOR_INTS = SECTOR_BYTES / 4;
const int SECTOR_COLS = 32;

static const char* const REGION_DAT_NAME = "chunks.dat";

static void logAssert(int actual, int expected) {
	if (actual != expected) {
		LOGI("ERROR: I/O operation failed (%d vs %d)\n", actual, expected);
	}
}

RegionFile::RegionFile(const std::string& basePath)
:	file(NULL)
{
	filename = basePath;
	filename += "/";
	filename += REGION_DAT_NAME;

	offsets = new int[SECTOR_INTS];

	emptyChunk = new int[SECTOR_INTS];
	memset(emptyChunk, 0, SECTOR_INTS * sizeof(int));
}

RegionFile::~RegionFile()
{
	close();
	delete [] offsets;
	delete [] emptyChunk;
}

bool RegionFile::open()
{
	close();
	sectorFree.clear();
	memset(offsets, 0, SECTOR_INTS * sizeof(int));

	file = fopen(filename.c_str(), "r+b");

	if (!file)
	{
		file = fopen(filename.c_str(), "w+b");

		if (!file)
		{
			LOGI("Failed to create chunk file %s\n",
				filename.c_str());
			return false;
		}

		if (fwrite(offsets, sizeof(int), SECTOR_INTS, file)
			!= SECTOR_INTS ||
			fflush(file) != 0)
		{
			LOGI("Failed to write region header: %s\n",
				filename.c_str());
			close();
			return false;
		}

		sectorFree[0] = false;
		return true;
	}

	if (fread(offsets, sizeof(int), SECTOR_INTS, file)
		!= SECTOR_INTS)
	{
		LOGI("Failed to read region header: %s\n",
			filename.c_str());
		close();
		return false;
	}

	if (fseek(file, 0, SEEK_END) != 0)
	{
		LOGI("Failed to seek to end of region file: %s\n",
			filename.c_str());
		close();
		return false;
	}

	long fileSize = ftell(file);

	if (fileSize < SECTOR_BYTES)
	{
		LOGI("Invalid region file size: %s\n",
			filename.c_str());
		close();
		return false;
	}

	// Recover the sector alignment if a previous write was interrupted.
	const long remainder = fileSize % SECTOR_BYTES;

	if (remainder != 0)
	{
		const int padding =
			static_cast<int>(SECTOR_BYTES - remainder);

		if (fseek(file, 0, SEEK_END) != 0 ||
			fwrite(emptyChunk, 1, padding, file)
			!= static_cast<size_t>(padding) ||
			fflush(file) != 0)
		{
			LOGI("Failed to repair region file alignment: %s\n",
				filename.c_str());
			close();
			return false;
		}

		fileSize += padding;
	}

	const long sectorCountLong = fileSize / SECTOR_BYTES;

	if (sectorCountLong > INT_MAX)
	{
		LOGI("Region file is too large: %s\n",
			filename.c_str());
		close();
		return false;
	}

	const int sectorCount =
		static_cast<int>(sectorCountLong);

	// Mark every existing sector as free first.
	// Then reserve the header and the sectors referenced by chunks.
	for (int sector = 0; sector < sectorCount; ++sector)
		sectorFree[sector] = true;

	sectorFree[0] = false;

	for (int i = 0; i < SECTOR_INTS; ++i)
	{
		const int offset = offsets[i];

		if (offset == 0)
			continue;

		const int base = offset >> 8;
		const int count = offset & 0xff;

		if (base <= 0 ||
			count <= 0 ||
			base >= sectorCount ||
			count > sectorCount - base)
		{
			LOGI("Invalid region offset at entry %d in %s\n",
				i, filename.c_str());
			continue;
		}

		for (int j = 0; j < count; ++j)
			sectorFree[base + j] = false;
	}

	return true;
}

void RegionFile::close()
{
	if (file)
	{
		fclose(file);
		file = NULL;
	}
}

bool RegionFile::readChunk(
	int x,
	int z,
	RakNet::BitStream** destChunkData)
{
	// Coordinates must be local to this 32 x 32 region.
	if (!file || !destChunkData ||
		x < 0 || x >= SECTOR_COLS ||
		z < 0 || z >= SECTOR_COLS)
	{
		LOGI("Invalid RegionFile::readChunk coordinates (%d, %d)\n",
			x, z);
		return false;
	}

	*destChunkData = NULL;

	const int index = x + z * SECTOR_COLS;
	const int offset = offsets[index];

	// This chunk has never been written to this region.
	if (offset == 0)
		return false;

	const int sectorNum = offset >> 8;
	const int sectorCount = offset & 0xff;

	// Sector zero contains the region index; the chunk must
	// occupy at least one other sector.
	if (sectorNum <= 0 ||
		sectorCount <= 0 ||
		sectorNum > LONG_MAX / SECTOR_BYTES)
	{
		LOGI("Invalid region offset for chunk (%d, %d)\n", x, z);
		return false;
	}

	const long sectorOffset =
		static_cast<long>(sectorNum) * SECTOR_BYTES;

	if (fseek(file, sectorOffset, SEEK_SET) != 0) {
		LOGI("Failed to seek to chunk (%d, %d)\n", x, z);
		return false;
	}

	int storedLength = 0;

	if (fread(&storedLength, sizeof(int), 1, file) != 1) {
		LOGI("Failed to read chunk length (%d, %d)\n", x, z);
		return false;
	}

	const int sectorCapacity = sectorCount * SECTOR_BYTES;

	// storedLength includes the four-byte length header.
	// A valid chunk must fit inside its allocated sectors.
	if (storedLength <= static_cast<int>(sizeof(int)) ||
		storedLength >= sectorCapacity)
	{
		LOGI("Invalid chunk length %d at (%d, %d)\n",
			storedLength, x, z);
		return false;
	}

	const int dataLength = storedLength - sizeof(int);

	unsigned char* data = new unsigned char[dataLength];

	if (fread(data, 1, dataLength, file) !=
		static_cast<size_t>(dataLength))
	{
		LOGI("Incomplete chunk data at (%d, %d)\n", x, z);
		delete[] data;
		return false;
	}

	*destChunkData =
		new RakNet::BitStream(data, dataLength, false);

	return true;
}

bool RegionFile::writeChunk(int x, int z,
	RakNet::BitStream& chunkData)
{
	if (!file ||
		x < 0 || x >= SECTOR_COLS ||
		z < 0 || z >= SECTOR_COLS)
	{
		LOGI("Invalid RegionFile::writeChunk coordinates (%d, %d)\n",
			x, z);
		return false;
	}

	const int index = x + z * SECTOR_COLS;
	const int payloadSize =
		static_cast<int>(chunkData.GetNumberOfBytesUsed());

	if (payloadSize <= 0 ||
		payloadSize > INT_MAX - static_cast<int>(sizeof(int)))
	{
		LOGI("Invalid chunk payload size: %d\n", payloadSize);
		return false;
	}

	const int storedSize =
		payloadSize + static_cast<int>(sizeof(int));

	const int sectorsNeeded =
		(storedSize + SECTOR_BYTES - 1) / SECTOR_BYTES;

	// The sector count occupies the low 8 bits of an offset.
	// A count of 256 would wrap to zero.
	if (sectorsNeeded > 255)
	{
		LOGI("Chunk is too big to be saved to file\n");
		return false;
	}

	const int oldOffset = offsets[index];
	int oldSector = oldOffset >> 8;
	int oldCount = oldOffset & 0xff;

	bool oldEntryValid = false;

	if (oldSector > 0 && oldCount > 0)
	{
		const int lastOldSector =
			oldSector + oldCount - 1;

		oldEntryValid =
			sectorFree.find(oldSector) != sectorFree.end() &&
			sectorFree.find(lastOldSector) != sectorFree.end();
	}

	if (!oldEntryValid)
	{
		oldSector = 0;
		oldCount = 0;
	}

	// Same allocation size: safely overwrite the existing record.
	if (oldSector != 0 && oldCount == sectorsNeeded)
		return write(oldSector, chunkData);

	// Do not free the old allocation yet. Keep the previous saved
	// chunk valid until the replacement data and its index are written.
	int slot = 0;
	int runLength = 0;
	bool extendFile = false;

	while (runLength < sectorsNeeded)
	{
		const int candidate = slot + runLength;

		FreeSectorMap::iterator it =
			sectorFree.find(candidate);

		if (it == sectorFree.end())
		{
			extendFile = true;
			break;
		}

		if (it->second)
		{
			++runLength;
		}
		else
		{
			slot = candidate + 1;
			runLength = 0;
		}
	}

	if (extendFile)
	{
		const int appendStart = slot + runLength;

		// open() keeps the sector map contiguous through the current
		// end of the file. Appending anywhere else would be unsafe.
		if (appendStart != static_cast<int>(sectorFree.size()))
		{
			LOGI("Region sector map is not contiguous\n");
			return false;
		}

		if (appendStart > LONG_MAX / SECTOR_BYTES ||
			fseek(file, 0, SEEK_END) != 0)
		{
			LOGI("Failed to seek while extending region file\n");
			return false;
		}

		const long expectedEnd =
			static_cast<long>(appendStart) * SECTOR_BYTES;

		if (ftell(file) != expectedEnd)
		{
			LOGI("Unexpected region file end position\n");
			return false;
		}

		const int sectorsToAppend =
			sectorsNeeded - runLength;

		for (int i = 0; i < sectorsToAppend; ++i)
		{
			if (fwrite(emptyChunk, sizeof(int),
				SECTOR_INTS, file) != SECTOR_INTS)
			{
				LOGI("Failed to extend region file\n");
				return false;
			}

			// These are the sectors actually appended at EOF.
			sectorFree[appendStart + i] = true;
		}

		if (fflush(file) != 0)
		{
			LOGI("Failed to flush extended region file\n");
			return false;
		}
	}

	if (slot <= 0 || slot > (INT_MAX >> 8))
	{
		LOGI("Invalid sector allocation: %d\n", slot);
		return false;
	}

	// Write the replacement record before changing the chunk index.
	if (!write(slot, chunkData))
		return false;

	for (int i = 0; i < sectorsNeeded; ++i)
		sectorFree[slot + i] = false;

	const int newOffset =
		(slot << 8) | sectorsNeeded;

	if (fseek(file, index * static_cast<int>(sizeof(int)),
		SEEK_SET) != 0 ||
		fwrite(&newOffset, sizeof(int), 1, file) != 1)
	{
		LOGI("Failed to update region index for chunk (%d, %d)\n",
			x, z);
		return false;
	}

	// Remember the new entry even if flushing fails. This prevents
	// the current process from reusing sectors the index may reference.
	offsets[index] = newOffset;

	if (fflush(file) != 0)
	{
		LOGI("Failed to flush region index for chunk (%d, %d)\n",
			x, z);
		return false;
	}

	// Only release the old allocation after committing the new index.
	for (int i = 0; i < oldCount; ++i)
		sectorFree[oldSector + i] = true;

	return true;
}

bool RegionFile::write(int sector, RakNet::BitStream& chunkData)
{
	if (!file ||
		sector <= 0 ||
		sector > LONG_MAX / SECTOR_BYTES)
	{
		LOGI("Invalid region write sector: %d\n", sector);
		return false;
	}

	const long sectorOffset =
		static_cast<long>(sector) * SECTOR_BYTES;

	if (fseek(file, sectorOffset, SEEK_SET) != 0)
	{
		LOGI("Failed to seek to sector %d\n", sector);
		return false;
	}

	const int payloadSize =
		static_cast<int>(chunkData.GetNumberOfBytesUsed());

	const int storedSize =
		payloadSize + static_cast<int>(sizeof(int));

	if (fwrite(&storedSize, sizeof(int), 1, file) != 1)
	{
		LOGI("Failed to write chunk length at sector %d\n", sector);
		return false;
	}

	if (payloadSize > 0 &&
		fwrite(chunkData.GetData(), 1, payloadSize, file)
		!= static_cast<size_t>(payloadSize))
	{
		LOGI("Failed to write chunk payload at sector %d\n", sector);
		return false;
	}

	if (fflush(file) != 0)
	{
		LOGI("Failed to flush chunk at sector %d\n", sector);
		return false;
	}

	return true;
}
