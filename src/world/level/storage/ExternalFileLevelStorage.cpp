#include <cstddef>
#include <fstream>
#include <ios>
#include <string>
#if !defined(DEMO_MODE) && !defined(APPLE_DEMO_PROMOTION)

#include "LevelData.h"
#include "RegionFile.h"
#include "ExternalFileLevelStorage.h"
#include "FolderMethods.h"
#include "../chunk/ChunkSource.h"
#include "../chunk/LevelChunk.h"
#include "../Level.h"
#include "../LevelConstants.h"
#include "platform/log.h"
#include "../tile/TreeTile.h"
#include "../../entity/EntityFactory.h"
#include "../../../nbt/NbtIo.h"
#include "../../../util/RakDataIO.h"
#include "../../../raknet/GetTime.h"
#include "../tile/entity/TileEntity.h"

static const int ChunkVersion_Light = 1;
static const int ChunkVersion_Entity = 2;

const char* const fnLevelDatOld = "level.dat_old";
const char* const fnLevelDatNew = "level.dat_new";
const char* const fnLevelDat    = "level.dat";
const char* const fnPlayerDat   = "player.dat";

//
// Helpers for converting old levels to newer
//
class LevelConverters
{
public:
	// Replacing old Cloth (id based) with new Cloth (data based)
	static bool v1_ClothIdToClothData(LevelChunk* c) {
		bool changed = false;
		unsigned char* blocks = c->getBlockData();
		unsigned char newTile = Tile::cloth->id;

		for (int i = 0; i < 16*16*128; ++i) {
			unsigned char oldTile = blocks[i];
			//Tile::cloth_00 to Tile::cloth_61
			if (oldTile >= 101 && oldTile <= 115) {
				int color = 0xf - (oldTile - 101);
				blocks[i] = newTile;
				c->data.set(i, color);
				changed = true;
			}
		}
		return changed;
	}

	// Replacing unavailable blocks with "Update!" blocks
	static bool ReplaceUnavailableBlocks(LevelChunk* c) {
		//int st = getTimeMs();
		
		bool changed = false;
		unsigned char* blocks = c->getBlockData();

		for (int i = 0; i < 16*16*128; ++i) {
			unsigned char oldTile = blocks[i];
			unsigned char newTile = Tile::transformToValidBlockId(oldTile);
			if (oldTile != newTile) {
				blocks[i] = newTile;
				changed = true;
			}
		}

		//int et = getTimeMs();
		//LOGI("time: %d\n", et - st);
		return changed;
	}

	//static bool ConvertPlayerDatToLevelDat() {
	//	return false;
	//}
};


ExternalFileLevelStorage::ExternalFileLevelStorage(
	const std::string& levelId,
	const std::string& fullPath)
	: levelId(levelId),
	levelPath(fullPath),
	loadedLevelData(NULL),
	regionFiles(),
	entitiesFile(NULL),
	level(NULL),
	tickCount(0),
	loadedStorageVersion(SharedConstants::StorageVersion),
	unsavedChunkList(),
	lastSavedEntitiesTick(-999999)
{
	createFolderIfNotExists(levelPath.c_str());

	std::string playerFolder = levelPath + "/players";
	createFolderIfNotExists(playerFolder.c_str());

	std::string datFileName = levelPath + "/" + fnLevelDat;
	std::string levelFileName = levelPath + "/" + fnPlayerDat;

	loadedLevelData = new LevelData();

	if (readLevelData(levelPath, *loadedLevelData))
	{
		loadedStorageVersion = loadedLevelData->getStorageVersion();
		readPlayerData(levelFileName, *loadedLevelData);
	}
	else
	{
		delete loadedLevelData;
		loadedLevelData = NULL;
	}
}

bool ExternalFileLevelStorage::hasSavedLighting() const
{
	return loadedStorageVersion >= ChunkVersion_Light;
}

ExternalFileLevelStorage::~ExternalFileLevelStorage()
{
	for (std::map<std::pair<int, int>, RegionFile*>::iterator it =
		regionFiles.begin();
		it != regionFiles.end(); ++it)
	{
		delete it->second;
	}

	regionFiles.clear();

	delete entitiesFile;
	delete loadedLevelData;
}

RegionFile* ExternalFileLevelStorage::getRegionFile(
	int chunkX,
	int chunkZ,
	int& localX,
	int& localZ,
	bool createIfMissing)
{
	const int REGION_SIZE = 32;

	// Convert global chunk coordinates to region + local coordinates.
	// The correction also handles negative coordinates if needed later.
	int regionX = chunkX / REGION_SIZE;
	int regionZ = chunkZ / REGION_SIZE;

	localX = chunkX % REGION_SIZE;
	localZ = chunkZ % REGION_SIZE;

	if (localX < 0) {
		localX += REGION_SIZE;
		--regionX;
	}

	if (localZ < 0) {
		localZ += REGION_SIZE;
		--regionZ;
	}

	const std::pair<int, int> key(regionX, regionZ);

	// Reuse an already-open region file.
	std::map<std::pair<int, int>, RegionFile*>::iterator it =
		regionFiles.find(key);

	if (it != regionFiles.end())
		return it->second;

	// Preserve the old save format for region (0, 0):
	// <world folder>/chunks.dat
	std::string regionPath = levelPath;

	// Other regions use their own folders:
	// <world folder>/region_X_Z/chunks.dat
	if (regionX != 0 || regionZ != 0) {
		regionPath += "/region_" +
			std::to_string(regionX) + "_" +
			std::to_string(regionZ);

		// Only create new region directories when saving.
		if (createIfMissing &&
			!createFolderIfNotExists(regionPath.c_str()))
		{
			LOGI("Could not create region folder: %s\n",
				regionPath.c_str());
			return NULL;
		}
	}

	const std::string regionFilename = regionPath + "/chunks.dat";

	// Reading a nonexistent region should mean "no saved chunk",
	// not creating a blank file for every region we visit.
	if (!createIfMissing && !exists(regionFilename.c_str()))
		return NULL;

	RegionFile* region = new RegionFile(regionPath);

	if (!region->open()) {
		LOGI("Could not open region (%d, %d): %s\n",
			regionX, regionZ, regionFilename.c_str());

		delete region;
		return NULL;
	}

	regionFiles.insert(std::make_pair(key, region));

	return region;
}

void ExternalFileLevelStorage::saveLevelData(LevelData& levelData, std::vector<Player*>* players) {
	ExternalFileLevelStorage::saveLevelData(levelPath, levelData, players);
}

void ExternalFileLevelStorage::saveLevelData( const std::string& levelPath, LevelData& levelData, std::vector<Player*>* players )
{
	// @todo: completely rewrite
	std::string directory = levelPath + "/";
    std::string tmpFile = directory + fnLevelDatNew;
    std::string datFile = directory + fnLevelDat;
    std::string oldFile = directory + fnLevelDatOld;

	levelData.setStorageVersion(SharedConstants::StorageVersion);
	if (!writeLevelData(tmpFile, levelData, players))
        return;

    // Remove old backup
    remove(oldFile.c_str());

    // If it exists, move the previous save to backup (and possibly delete it)
    if (exists(datFile.c_str())) {
        if (rename(datFile.c_str(), oldFile.c_str())) {
            LOGE("Error@saveLevelData: Couldn't move savefile to level.dat_old\n");
            return;
        }
        remove(datFile.c_str());
    }
    // Move the new save to level.dat
    if (rename(tmpFile.c_str(), datFile.c_str())) {
        LOGE("Error@saveLevelData: Couldn't move new file to level.dat\n");
        return;
    }

    // Remove the temporary save, if the rename didn't do it
    remove(tmpFile.c_str());

	// Save players
	// fuck mojang for that
	if (!players || players->empty()) {
		return;
	}

	for (auto& player : *players) {
		if (player != NULL) {
			savePlayer(*player, directory);
		}
	}
}

void ExternalFileLevelStorage::savePlayer(Player& player, const std::string& worldDir) {
	std::string playerPath = worldDir + "/players/" + player.name + ".dat";

	LOGI("Saving player %s to %s...\n", player.name.c_str(), playerPath.c_str());

	RakNet::BitStream data;
	RakDataOutput buf(data);
	CompoundTag playerTag;
	player.saveWithoutId(&playerTag);

	NbtIo::write(&playerTag, &buf);

	std::ofstream file(playerPath, std::ios::out | std::ios::binary);
	file.write((const char*)data.GetData(), (size_t)data.GetNumberOfBytesUsed());
}

bool ExternalFileLevelStorage::loadPlayer(Player& player, const std::string& worldDir) {
	std::string playerPath = worldDir + "/players/" + player.name + ".dat";

	LOGI("Loading player %s from %s...\n", player.name.c_str(), playerPath.c_str());

	std::ifstream file(playerPath, std::ios::in | std::ios::binary);
	if (!file.is_open()) {
		return false;
	}

	std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	RakNet::BitStream bitStream(data.data(), data.size(), false);
	RakDataInput stream(bitStream);

	CompoundTag* tag = NbtIo::read(&stream);
	if (tag) {
		player.load(tag);
		tag->deleteChildren();
		delete tag;
	}

	return true;
}

void ExternalFileLevelStorage::savePlayer(Player& player) {
	ExternalFileLevelStorage::savePlayer(player, levelPath);
}

bool ExternalFileLevelStorage::loadPlayer(Player& player) {
	return ExternalFileLevelStorage::loadPlayer(player, levelPath);
}

LevelData* ExternalFileLevelStorage::prepareLevel(Level* _level)
{
	level = _level;
	return loadedLevelData;
}

bool ExternalFileLevelStorage::readLevelData(const std::string& directory, LevelData& levelData)
{
    // Try to load level.dat
    std::string datFilename = directory + "/" + fnLevelDat;
	FILE* file = fopen(datFilename.c_str(), "rb");

    // If that fails, try to load level.dat_old
    if (!file) {
        datFilename = directory + "/" + fnLevelDatOld;
        file = fopen(datFilename.c_str(), "rb");
    }

    if (!file)
        return false;

	int version = 0;
	int size = 0;
	unsigned char* data = NULL;

	do {
		if (fread(&version, sizeof(version), 1, file) != 1)
		{
			break;
		}
		if (fread(&size, sizeof(size), 1, file) != 1)
		{
			break;
		}

		int left = getRemainingFileSize(file);
		if (size > left || size <= 0)
			break;

		data = new unsigned char[size];
		if (fread(data, 1, size, file) != size)
		{
			break;
		}

		if (version == 1) {
			RakNet::BitStream bitStream(data, size, false);
			levelData.v1_read(bitStream, version);
		} else if (version >= 2) {
			//LOGI("---> Trying to load level with version %d\n", version);
			RakNet::BitStream bitStream(data, size, false);
			RakDataInput stream(bitStream);
			//LOGI("dat: %s\n", datFileName.c_str());
			CompoundTag* tag = NbtIo::read(&stream);
			if (tag) {
				levelData.getTagData(tag);
				tag->deleteChildren();
				delete tag;
			}
			//LOGI("<--- Finished reading level tag: %p\n", tag);
		}
	} while (false);

	fclose(file);
	delete [] data;

    return true;
}

bool ExternalFileLevelStorage::writeLevelData(const std::string& datFileName, LevelData& levelData, const std::vector<Player*>* players)
{
	LOGI("Writing down level seed as: %ld\n", levelData.getSeed());
	//return true;

	// Write level info
	FILE* file = fopen(datFileName.c_str(), "wb");
	if (!file)
        return false;

    //if (levelData.getStorageVersion() == 1) {
	RakNet::BitStream data;
	if (levelData.getStorageVersion() == 1)
		levelData.v1_write(data);
	else {
		RakDataOutput buf(data);
		//LOGI("---> Trying to write level with version %d\n", version);
		CompoundTag* tag = NULL;
		if (players && !players->empty())
			tag = levelData.createTag(*players);
		else
			tag = levelData.createTag();

		NbtIo::write(tag, &buf);
		tag->deleteChildren();
		delete tag;
		//LOGI("<--- Finished writing level data. Size: %d\n", fdout.bytesWritten);
	}

	int version = levelData.getStorageVersion(); // 1
	fwrite(&version, sizeof(version), 1, file);
	int size = data.GetNumberOfBytesUsed();
	fwrite(&size, sizeof(size), 1, file);

	fwrite(data.GetData(), 1, size, file);
	fclose(file);

	return true;
}

bool ExternalFileLevelStorage::readPlayerData(const std::string& filename, LevelData& dest)
{
	FILE* fp = fopen(filename.c_str(), "rb");
	if (!fp)
		return false;

	do {
		int version;
		if (fread(&version, 4, 1, fp) != 1)
			break;

		int size;
		if (fread(&size, 4, 1, fp) != 1)
			break;

		if (version == 1) {
			if (fread(&dest.playerData, 1, sizeof(dest.playerData), fp) != size)
				break;

			// Fix coordinates
			Vec3& pos = dest.playerData.pos;
			if (pos.x < 0.5f) pos.x = 0.5f;
			if (pos.z < 0.5f) pos.z = 0.5f;
			if (pos.x > (LevelConstants::LEVEL_WIDTH - 0.5f)) pos.x = LevelConstants::LEVEL_WIDTH - 0.5f;
			if (pos.z > (LevelConstants::LEVEL_DEPTH - 0.5f)) pos.z = LevelConstants::LEVEL_DEPTH - 0.5f;
			if (pos.y < 0) pos.y = 64;

			dest.playerDataVersion = version;
		}
	} while (false);

	fclose(fp);
	return true;
}

void ExternalFileLevelStorage::tick()
{
	tickCount++;

	if ((tickCount % 1000) == 0 && level) {
		LOGI("Saving level...\n");

		std::vector<LevelChunk*> loadedChunks;

		if (level->getChunkSource())
			level->getChunkSource()->getLoadedChunks(loadedChunks);

		for (std::vector<LevelChunk*>::iterator it =
			loadedChunks.begin();
			it != loadedChunks.end(); ++it)
		{
			LevelChunk* chunk = *it;

			if (!chunk || !chunk->unsaved)
				continue;

			int pos = chunk->x +
				chunk->z * LevelConstants::CHUNK_CACHE_WIDTH;

			UnsavedChunkList::iterator prev =
				unsavedChunkList.begin();

			for (; prev != unsavedChunkList.end(); ++prev) {
				if (prev->pos == pos) {
					prev->addedToList = RakNet::GetTimeMS();
					break;
				}
			}

			if (prev == unsavedChunkList.end()) {
				UnsavedLevelChunk unsaved;
				unsaved.pos = pos;
				unsaved.addedToList = RakNet::GetTimeMS();
				unsaved.chunk = chunk;
				unsavedChunkList.push_back(unsaved);
			}

			// This chunk is queued for saving.
			chunk->unsaved = false;
		}

		savePendingUnsavedChunks(2);
	}

	if (tickCount - lastSavedEntitiesTick >
		(60 * SharedConstants::TicksPerSecond)) {
		saveEntities(level, NULL);
	}
}

void ExternalFileLevelStorage::save(Level* level, LevelChunk* levelChunk)
{
	if (!levelChunk)
		return;

	int localX = 0;
	int localZ = 0;

	RegionFile* region = getRegionFile(
		levelChunk->x,
		levelChunk->z,
		localX,
		localZ,
		true
	);

	if (!region) {
		levelChunk->unsaved = true;
		LOGI("Failed to get region for chunk (%d, %d)\n",
			levelChunk->x, levelChunk->z);
		return;
	}

	// Preserve betaPE's existing chunk data layout.
	RakNet::BitStream chunkData;

	chunkData.Write(
		(const char*)levelChunk->getBlockData(),
		LevelConstants::CHUNK_BLOCK_COUNT
	);

	chunkData.Write(
		(const char*)levelChunk->data.data,
		LevelConstants::CHUNK_BLOCK_COUNT / 2
	);

	chunkData.Write(
		(const char*)levelChunk->skyLight.data,
		LevelConstants::CHUNK_BLOCK_COUNT / 2
	);

	chunkData.Write(
		(const char*)levelChunk->blockLight.data,
		LevelConstants::CHUNK_BLOCK_COUNT / 2
	);

	chunkData.Write(
		(const char*)levelChunk->updateMap,
		LevelConstants::CHUNK_COLUMNS
	);

	if (!region->writeChunk(localX, localZ, chunkData)) {
		levelChunk->unsaved = true;

		LOGI("Failed to write chunk (%d, %d), local (%d, %d)\n",
			levelChunk->x, levelChunk->z,
			localX, localZ);
		return;
	}

	levelChunk->unsaved = false;
}

LevelChunk* ExternalFileLevelStorage::load(Level* level, int x, int z)
{
	int localX = 0;
	int localZ = 0;

	RegionFile* region = getRegionFile(
		x,
		z,
		localX,
		localZ,
		false
	);

	if (!region)
		return NULL;

	RakNet::BitStream* chunkData = NULL;

	if (!region->readChunk(localX, localZ, &chunkData)) {
		return NULL;
	}

	chunkData->ResetReadPointer();

	unsigned char* blockIds = new unsigned char[LevelConstants::CHUNK_BLOCK_COUNT];
	chunkData->Read((char*)blockIds, LevelConstants::CHUNK_BLOCK_COUNT);

	LevelChunk* levelChunk = new LevelChunk(level, blockIds, x, z);
	chunkData->Read((char*)levelChunk->data.data, LevelConstants::CHUNK_BLOCK_COUNT / 2);
	if (loadedStorageVersion >= ChunkVersion_Light) {
		chunkData->Read((char*)levelChunk->skyLight.data, LevelConstants::CHUNK_BLOCK_COUNT / 2);
		chunkData->Read((char*)levelChunk->blockLight.data, LevelConstants::CHUNK_BLOCK_COUNT / 2);
	}
	chunkData->Read((char*)levelChunk->updateMap, LevelConstants::CHUNK_COLUMNS);
	// This will be difficult to maintain.. Storage version could be per chunk
	// too (but probably better to just read all -> write all, so that all
	// chunks got same version anyway)
	//if (loadedStorageVersion >= ChunkVersion_Entity) {
	//	int dictSize;
	//	chunkData->Read(dictSize);

	//	RakDataInput dis(*chunkData);
	//	Tag* tmp = Tag::readNamedTag(&dis);
	//	if (tmp && tmp->getId() == Tag::TAG_Compound) {
	//		CompoundTag* tag = (CompoundTag*) tmp;

	//		delete tmp;
	//	}
	//}

	delete [] chunkData->GetData();
	delete chunkData;

	//bool dbg = (x == 7 && z == 9);

	//int t = 0;
	//for (int i = 0; i < LevelConstants::CHUNK_COLUMNS; ++i) {
	//	char bits = levelChunk->updateMap[i];
	//	t += (bits != 0);
	//	int xx = x * 16 + i%16;
	//	int zz = z * 16 + i/16;
	//	if (dbg && xx == 125 && zz == 152) {
	//		LOGI("xz: %d, %d: %d\n", xx, zz, bits);
	//		for (int j = 0; j < 8; ++j) {
	//			if (bits & (1 << j)) {
	//				LOGI("%d - %d\n", j << 4, ((j+1) << 4) - 1);
	//			}
	//		}
	//	}
	//}

	//
	// Convert LevelChunks here if necessary
	//
	//LOGI("level version: %d: upd: %d - (%d, %d)\n", loadedStorageVersion, t, x, z);
	bool changed = false;

	// Loaded level has old Cloth types (one Tile* per color)
	if (loadedStorageVersion == 1)
		changed |= LevelConverters::v1_ClothIdToClothData(levelChunk);

	// Loaded level is newer than our level - replace all unavailable block types
	//if (loadedStorageVersion > SharedConstants::StorageVersion)
		changed |= LevelConverters::ReplaceUnavailableBlocks(levelChunk);

		if (loadedStorageVersion >= ChunkVersion_Light && !changed)
		{
			// Saved light arrays are valid; recalculate heights only.
			levelChunk->recalcHeightmapOnly();
		}
		else
		{
			// Legacy or converted chunk data needs lighting recalculation.
			levelChunk->recalcHeightmap();
		}

		levelChunk->unsaved = changed;
	levelChunk->terrainPopulated = true;
	levelChunk->createdFromSave = true;

	return levelChunk;
}

void ExternalFileLevelStorage::saveEntities( Level* level, LevelChunk* levelChunk )
{
	lastSavedEntitiesTick = tickCount;
	int count = 0;
	float st = getTimeS();

	// Version 1: Save ALL Entities for all chunks in one structure
	EntityList& entities = level->entities;

	ListTag* entityTags = new ListTag();
	for (unsigned int i = 0; i < entities.size(); ++i) {
		Entity* e = entities[i];

		CompoundTag* tag = new CompoundTag();
		if (e->save(tag)) {
			count++;
			entityTags->add(tag);
		} else
			delete tag;
	}

	// Version 1: Save ALL TileEntities for all chunks in one structure
	TileEntityList& tileEntities = level->tileEntities;
	//TileEntityList keep, dontKeep;
	//partitionTileEntities

	ListTag* tileEntityTags = new ListTag();
	for (unsigned int i = 0; i < tileEntities.size(); ++i) {
		TileEntity* e = tileEntities[i];
		if (!e->shouldSave()) continue;

		CompoundTag* tag = new CompoundTag();
		if (e->save(tag)) {
			count++;
			tileEntityTags->add(tag);
		} else
			delete tag;
	}

	CompoundTag base;
	base.put("Entities", entityTags);
	base.put("TileEntities", tileEntityTags);

	RakNet::BitStream stream;
	RakDataOutput dos(stream);
	NbtIo::write(&base, &dos);
	int numBytes = stream.GetNumberOfBytesUsed();

	FILE* fp = fopen((levelPath + "/entities.dat").c_str(), "wb");
	if (fp) {
		int version = 1;
		fwrite("ENT\0", 1, 4, fp);
		fwrite(&version, sizeof(int), 1, fp);
		fwrite(&numBytes, sizeof(int), 1, fp);
		fwrite(stream.GetData(), 1, numBytes, fp);
		fclose(fp);
	}

	base.deleteChildren();

	float tt = getTimeS() - st;
	LOGI("Time to save %d entities: %f s. Size: %d bytes\n", count, tt, numBytes);
}

void ExternalFileLevelStorage::loadEntities(Level* level, LevelChunk* chunk) {
	lastSavedEntitiesTick = tickCount;
	FILE* fp = fopen((levelPath + "/entities.dat").c_str(), "rb");
	if (fp) {
		char header[5];
		int version, numBytes;
		fread(header, 1, 4, fp);
		fread(&version, sizeof(int), 1, fp);
		fread(&numBytes, sizeof(int), 1, fp);

		int left = getRemainingFileSize(fp);
		if (numBytes <= left && numBytes > 0) {
			unsigned char* buf = new unsigned char[numBytes];

			fread(buf, 1, numBytes, fp);

			RakNet::BitStream stream(buf, numBytes, false);
			RakDataInput dis(stream);

			CompoundTag* tag = NbtIo::read(&dis);
			//
			// Read Entity:es
			//
			if (tag->contains("Entities", Tag::TAG_List)) {
				ListTag* entityTags = tag->getList("Entities");
				for (int i = 0; i < entityTags->size(); ++i) {
					Tag* _et = entityTags->get(i);
					if (!_et || _et->getId() != Tag::TAG_Compound) {
						LOGE("Entity tag is either NULL or not a compoundTag: %p : %d!\n", _et, _et?_et->getId() : -1);
						continue;
					}
					CompoundTag* et = (CompoundTag*)_et;
					if (Entity* e = EntityFactory::loadEntity(et, level)) {
						level->addEntity(e);
					}
				}
			}
			//
			// Read TileEntity:s
			//
			if (tag->contains("TileEntities", Tag::TAG_List)) {
				ListTag* tileEntityTags = tag->getList("TileEntities");
				for (int i = 0; i < tileEntityTags->size(); ++i) {
					Tag* _et = tileEntityTags->get(i);
					if (!_et || _et->getId() != Tag::TAG_Compound) {
						LOGE("TileEntity tag is either NULL or not a compoundTag: %p : %d!\n", _et, _et?_et->getId() : -1);
						continue;
					}
					CompoundTag* et = (CompoundTag*)_et;
					if (TileEntity* e = TileEntity::loadStatic(et)) {
						
						LevelChunk* chunk = level->getChunkAt(e->x, e->z);
						if (chunk && !chunk->hasTileEntityAt(e)) {
							LOGI("Adding TileEntity %d to %d, %d, %d\n", e->type, e->x, e->y, e->z);
							chunk->addTileEntity(e);
						} else {
							if (!chunk)
								LOGE("Couldn't find chunk at %d, %d to add %d\n", e->x, e->z, e->type);
							else
								LOGE("Already have TileEntity at %d, %d to add %d\n", e->x, e->z, e->type);
							delete e;
						}
					}
				}
			}

			tag->deleteChildren();
			delete tag;

			delete[] buf;
		}
		LOGI("header: %s, version: %d, bytes: %d (remaining: %d)\n", header, version, numBytes, left);

		//fread(stream.GetData(), 1, numBytes, fp);
		fclose(fp);
	}
}

void ExternalFileLevelStorage::saveGame(Level* level) {
	saveEntities(level, NULL);
}

int ExternalFileLevelStorage::savePendingUnsavedChunks( int maxCount ) {
    if (maxCount < 0)
        maxCount = unsavedChunkList.size();

    int count = 0;
    while (++count <= maxCount && !unsavedChunkList.empty()) {

        UnsavedChunkList::iterator it = unsavedChunkList.begin();
        UnsavedChunkList::iterator remove = unsavedChunkList.begin();
        UnsavedLevelChunk* oldest = &(*it);

        for ( ; it != unsavedChunkList.end(); ++it) {
            if ((*it).addedToList < oldest->addedToList) {
                oldest = &(*it);
                remove = it;
            }
        }
        LevelChunk* chunk = oldest->chunk;
        unsavedChunkList.erase(remove);

        save(level, chunk);
    }
    return count;
}

void ExternalFileLevelStorage::saveAll( Level* level, std::vector<LevelChunk*>& levelChunks ) {
    ChunkStorage::saveAll(level, levelChunks);
	int numChunks = savePendingUnsavedChunks(-1);
    LOGI("Saving %d additional chunks.\n", numChunks);
}

#endif /*DEMO_MODE*/
