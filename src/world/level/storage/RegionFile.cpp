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

	memset(offsets, 0, SECTOR_INTS * sizeof(int));

	// attempt to open file
	file = fopen(filename.c_str(), "r+b");
	if (file)
	{
		// read offset table
		logAssert(fread(offsets, sizeof(int), SECTOR_INTS, file), SECTOR_INTS);

		// mark initial sector as blocked
		sectorFree[0] = false;

		// setup blocked sectors
		for (int sector = 0; sector < SECTOR_INTS; sector++)
		{
			int offset = offsets[sector];
			if (offset)
			{
				int base = offset >> 8;
				int count = offset & 0xff;

				for (int i = 0; i < count; i++)
				{
					sectorFree[base + i] = false;
				}
			}
		}
	}
	else
	{
		// new world
		file = fopen(filename.c_str(), "w+b");
		if (!file)
		{
			LOGI("Failed to create chunk file %s\n", filename.c_str());
			return false;
		}

		// write sector header (all zeroes)
		logAssert(fwrite(offsets, sizeof(int), SECTOR_INTS, file), SECTOR_INTS);

		// mark initial sector as blocked
		sectorFree[0] = false;
	}

	return file != NULL;
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

bool RegionFile::writeChunk(int x, int z, RakNet::BitStream& chunkData)
{
	if (!file ||
		x < 0 || x >= SECTOR_COLS ||
		z < 0 || z >= SECTOR_COLS)
	{
		LOGI("Invalid RegionFile::writeChunk coordinates (%d, %d)\n",
			x, z);
		return false;
	}

	int size = chunkData.GetNumberOfBytesUsed() + sizeof(int);

	int offset = offsets[x + z * SECTOR_COLS];
	int sectorNum = offset >> 8;
	int sectorCount = offset & 0xff;
	int sectorsNeeded = (size / SECTOR_BYTES) + 1;

	if (sectorsNeeded > 256)
	{
		LOGI("ERROR: Chunk is too big to be saved to file\n");
		return false;
	}

	if (sectorNum != 0 && sectorCount == sectorsNeeded) {
		// the sector can be written on top of its old data
		write(sectorNum, chunkData);
	}
	else
	{
		// we need a new location

		// mark old sectors as free
		for (int i = 0; i < sectorCount; i++)
		{
			sectorFree[sectorNum + i] = true;
		}

		// find an available slot with enough run length
		int slot = 0;
		int runLength = 0;
		bool extendFile = false;
		while (runLength < sectorsNeeded)
		{
			if (sectorFree.find(slot + runLength) == sectorFree.end())
			{
				extendFile = true;
				break;
			}
			if (sectorFree[slot + runLength] == true)
			{
				runLength++;
			}
			else
			{
				slot = slot + runLength + 1;
				runLength = 0;
			}
		}

		if (extendFile)
		{
			fseek(file, 0, SEEK_END);
			for (int i = 0; i < (sectorsNeeded - runLength); i++)
			{
				fwrite(emptyChunk, sizeof(int), SECTOR_INTS, file);
				sectorFree[slot + i] = true;
			}
		}
		offsets[x + z * SECTOR_COLS] = (slot << 8) | sectorsNeeded;
		// mark slots as taken
		for (int i = 0; i < sectorsNeeded; i++)
		{
			sectorFree[slot + i] = false;
		}

		// write!
		write(slot, chunkData);

		// write sector data
		fseek(file, (x + z * SECTOR_COLS) * sizeof(int), SEEK_SET);
		fwrite(&offsets[x + z * SECTOR_COLS], sizeof(int), 1, file);
	}


	return true;
}

bool RegionFile::write(int sector, RakNet::BitStream& chunkData)
{
	fseek(file, sector * SECTOR_BYTES, SEEK_SET);
	//LOGI("writing %d B to sector %d\n", chunkData.GetNumberOfBytesUsed(), sector);
	int size = chunkData.GetNumberOfBytesUsed() + sizeof(int);
	logAssert(fwrite(&size, sizeof(int), 1, file), 1);
	logAssert(fwrite(chunkData.GetData(), 1, chunkData.GetNumberOfBytesUsed(), file), chunkData.GetNumberOfBytesUsed());

	return true;
}
