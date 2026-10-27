
#include "ClientSideNetworkHandler.h"
#include "client/Options.h"
#include "network/packet/LoginStatusPacket.h"
#include "packet/PacketInclude.h"
#include "RakNetInstance.h"
#include "../world/level/chunk/ChunkSource.h"
#include "../world/level/Level.h"
#include "../world/level/storage/LevelStorageSource.h"
#include "../world/entity/player/Player.h"
#include "../world/entity/player/Inventory.h"
#include "../client/Minecraft.h"
#include "../client/gamemode/GameMode.h"
#include "world/item/ItemInstance.h"
#include "world/level/LevelConstants.h"
#include <cstddef>
#ifndef STANDALONE_SERVER
#include "../client/gui/screens/DisconnectionScreen.h"
#endif
#include "../client/player/LocalPlayer.h"
#include "../client/multiplayer/MultiPlayerLevel.h"
#include "../client/player/input/KeyboardInput.h"
#include "../client/sound/SoundEngine.h"
#include "../world/entity/MobFactory.h"
#include "../raknet/RakPeerInterface.h"
#include "../world/level/Explosion.h"
#include "../world/level/tile/entity/FurnaceTileEntity.h"
#include "../world/inventory/BaseContainerMenu.h"
#ifndef STANDALONE_SERVER
#include "../client/particle/TakeAnimationParticle.h"
#endif
#include "../world/entity/EntityFactory.h"
#include "../world/entity/item/PrimedTnt.h"
#include "../world/entity/projectile/Arrow.h"
#include "../world/level/tile/entity/ChestTileEntity.h"
#include "../client/player/RemotePlayer.h"
#include "../world/level/tile/LevelEvent.h"
#include "../world/entity/item/FallingTile.h"

static MultiPlayerLevel* mpcast(Level* l) { return (MultiPlayerLevel*) l; }

ClientSideNetworkHandler::ClientSideNetworkHandler(
	Minecraft* minecraft,
	IRakNetInstance* raknetInstance)
	: minecraft(minecraft),
	level(NULL),
	raknetInstance(raknetInstance),
	requestNextChunkPosition(0),
	NumRequestChunks(0),
	requestNextChunkIndex(0),
	loadedChunkCount(0)
{
	NumRequestChunks =
		LevelConstants::CHUNK_CACHE_WIDTH *
		LevelConstants::CHUNK_CACHE_WIDTH;

	requestNextChunkIndexList.resize(NumRequestChunks);
	chunksLoaded.resize(NumRequestChunks);

	clearChunksLoaded();

	rakPeer = raknetInstance->getPeer();
}

ClientSideNetworkHandler::~ClientSideNetworkHandler()
{
}

void ClientSideNetworkHandler::requestNextChunk()
{
	if (requestNextChunkIndex < NumRequestChunks)
	{
		const IntPair& chunk =
			requestNextChunkIndexList[requestNextChunkIndex];

		RequestChunkPacket packet(chunk.x, chunk.y);
		raknetInstance->send(packet);

		++requestNextChunkIndex;
		++requestNextChunkPosition;
		return;
	}

	// If every initial request has been sent but a chunk is still
	// missing, request the first missing chunk again.
	if (areAllChunksLoaded())
		return;

	const int width = LevelConstants::CHUNK_CACHE_WIDTH;

	for (int i = 0; i < NumRequestChunks; ++i)
	{
		const IntPair& chunk = requestNextChunkIndexList[i];

		if (chunk.x < 0 || chunk.x >= width ||
			chunk.y < 0 || chunk.y >= width)
		{
			continue;
		}

		const int index = chunk.x * width + chunk.y;

		if (index >= 0 &&
			index < static_cast<int>(chunksLoaded.size()) &&
			!chunksLoaded[index])
		{
			RequestChunkPacket packet(chunk.x, chunk.y);
			raknetInstance->send(packet);
			return;
		}
	}
}

bool ClientSideNetworkHandler::areAllChunksLoaded()
{
	return NumRequestChunks > 0 &&
		loadedChunkCount >= NumRequestChunks;
}

bool ClientSideNetworkHandler::isChunkLoaded(int x, int z)
{
	const int width = LevelConstants::CHUNK_CACHE_WIDTH;

	if (x < 0 || x >= width ||
		z < 0 || z >= width)
	{
		return false;
	}

	const int index = x * width + z;

	if (index < 0 ||
		index >= static_cast<int>(chunksLoaded.size()))
	{
		return false;
	}

	return chunksLoaded[index];
}

void ClientSideNetworkHandler::onConnect(const RakNet::RakNetGUID& hostGuid)
{
	LOGI("onConnect, server guid: %s, local guid: %s\n", hostGuid.ToString(), rakPeer->GetMyGUID().ToString());
	serverGuid = hostGuid;

	clearChunksLoaded();
	LoginPacket packet(minecraft->options.getStringValue(OPTIONS_USERNAME).c_str(), SharedConstants::NetworkProtocolVersion, true);
	raknetInstance->send(packet);
}

void ClientSideNetworkHandler::onUnableToConnect()
{
	LOGI("onUnableToConnect\n");
}

void ClientSideNetworkHandler::onDisconnect(const RakNet::RakNetGUID& guid)
{
	LevelConstants::CHUNK_CACHE_WIDTH = 16;
	LevelConstants::LEVEL_WIDTH = LevelConstants::CHUNK_CACHE_WIDTH * LevelConstants::CHUNK_WIDTH;
	LevelConstants::LEVEL_DEPTH = LevelConstants::CHUNK_CACHE_WIDTH * LevelConstants::CHUNK_DEPTH;
	
	// TODO: Good disconnecting
	LOGI("onDisconnect\n");
	if (level)
	{
		level->isClientSide = false;
		for (int i = (int)level->players.size()-1; i >= 0; --i ) {
			Player* p = level->players[i];
			if (p != minecraft->player) {
				p->reallyRemoveIfPlayer = true;
				level->removeEntity(p);
			}
		}
	}
	minecraft->leaveGame();	
// 	minecraft->setLevel(NULL);
// #ifndef STANDALONE_SERVER
// 	minecraft->gui.addMessage("Disconnected from server");
// 	// TODO: screen with error
//     minecraft->screenChooser.setScreen(SCREEN_STARTMENU);
// #endif
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, LoginStatusPacket* packet) {
	if (packet->status == LoginStatus::Success) {
		raknetInstance->setIsLoggedIn(true);
		return;
	}

	if (packet->status == LoginStatus::Failed_ClientOld) {
		LOGI("Disconnect! Client is outdated!\n");
#ifndef STANDALONE_SERVER
		minecraft->setScreen(new DisconnectionScreen("Could not connect: Outdated client!"));
#endif
	}
	if (packet->status == LoginStatus::Failed_ServerOld) {
		LOGI("Disconnect! Server is outdated!\n");
#ifndef STANDALONE_SERVER
		minecraft->setScreen(new DisconnectionScreen("Could not connect: Outdated server!"));
#endif
	}
	if (packet->status == LoginStatus::Failed_TakenNickname) {
		LOGI("Disconnect! Nickname is taken!\n");
#ifndef STANDALONE_SERVER
		minecraft->setScreen(new DisconnectionScreen("Could not connect: Nickname is taken!"));
#endif
	}
	if (packet->status == LoginStatus::Failed_Banned) {
		LOGI("Disconnect! You're banned from this server!\n");
#ifndef STANDALONE_SERVER
		minecraft->setScreen(new DisconnectionScreen("Could not connect: You're banned from this server!"));
#endif
	}
}


void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, StartGamePacket* packet)

{
	LOGI("StartGamePacket\n");

#ifdef RPI
	if (packet->gameType != GameType::Creative) {
		minecraft->setScreen(new DisconnectionScreen("Could not connect: Incompatible server!"));
		return;
	}
#endif

	// Read the host's world dimensions.
	const int serverChunkWidth =
		packet->chunkCacheWidth > 0
		? packet->chunkCacheWidth
		: LevelConstants::CHUNK_CACHE_WIDTH;

	const int serverWorldWidth =
		serverChunkWidth * LevelConstants::CHUNK_WIDTH;

	const int serverWorldDepth =
		serverChunkWidth * LevelConstants::CHUNK_DEPTH;

	LOGI(
		"Host world size: chunks=%d width=%d depth=%d\n",
		serverChunkWidth,
		serverWorldWidth,
		serverWorldDepth
	);

	// Apply the host's dimensions before creating the multiplayer level.
	LevelConstants::CHUNK_CACHE_WIDTH = serverChunkWidth;
	LevelConstants::LEVEL_WIDTH = serverWorldWidth;
	LevelConstants::LEVEL_DEPTH = serverWorldDepth;
	LevelChunk::ChunkBlockCount = LevelConstants::CHUNK_BLOCK_COUNT;

	// Initialise chunk tracking exactly once.
	NumRequestChunks = serverChunkWidth * serverChunkWidth;
	requestNextChunkIndexList.resize(NumRequestChunks);
	chunksLoaded.assign(NumRequestChunks, false);

	const std::string& levelId = LevelStorageSource::TempLevelId;
	LevelStorageSource* storageSource = minecraft->getLevelSource();

	storageSource->deleteLevel(levelId);

	MultiPlayerLevel* level = new MultiPlayerLevel(
		storageSource->selectLevel(levelId, true),
		"temp",
		LevelSettings(
			packet->levelSeed,
			LevelSettings::validateGameType(packet->gameType),
			false,
			serverWorldWidth
		),
		packet->levelGeneratorVersion
	);

	// Log AFTER construction, because _init() may change the globals.
	LOGI(
		"After multiplayer level creation: "
		"chunks=%d width=%d depth=%d tracked=%llu\n",
		LevelConstants::CHUNK_CACHE_WIDTH,
		LevelConstants::LEVEL_WIDTH,
		LevelConstants::LEVEL_DEPTH,
		static_cast<unsigned long long>(chunksLoaded.size())
	);

	level->isClientSide = true;

	bool isCreative = (packet->gameType == GameType::Creative);
	LocalPlayer* player = new LocalPlayer(minecraft, level, minecraft->options.getStringValue(OPTIONS_USERNAME), level->dimension->id, isCreative);
	player->owner = rakPeer->GetMyGUID();
	player->entityId = packet->entityId;
	player->moveTo(packet->x, packet->y, packet->z, player->yRot, player->xRot);
	player->waitingForInitialChunk = true;

	LOGI("new pos: %f, %f [%f - %f]\n", player->x, player->z, player->bb.y0, player->bb.y1);

	minecraft->setLevel(level, "ClientSideNetworkHandler -> setLevel", player);
	minecraft->setIsCreativeMode(isCreative);
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, MessagePacket* packet)
{
	LOGI("MessagePacket\n");
#ifndef STANDALONE_SERVER
	minecraft->gui.addMessage(packet->message.C_String());
#endif
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, SetTimePacket* packet)
{
	if (!level)
		return;

	// LOGI("SetTimePacket\n");
	level->setTime(packet->time);
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, AddEntityPacket* packet)
{
	if (!level)
		return;

	Entity* e = EntityFactory::CreateEntity(packet->type, level);
	if (!e)
		return;

	e->entityId = packet->entityId;
	e->setPos(packet->x, packet->y, packet->z);

	// Entity Specific stuff here
	switch (packet->type)
	{
		case EntityTypes::IdFallingTile: {
			int data = -packet->data();
            FallingTile* ft = (FallingTile*) e;
            ft->tile = data & 0xff;
            ft->data = data >> 16;
		}
		case EntityTypes::IdArrow: {
			Entity* owner = level->getEntity(packet->data());
			if (owner && owner->isMob())
				((Arrow*)e)->ownerId = owner->entityId;
			break;
		}
		default:
			break;
	}

    if (packet->hasMovementData()) {
		/*
		e->xd = packet->xd;
		e->yd = packet->yd;
		e->zd = packet->zd;
		*/
		e->lerpMotion(packet->xd, packet->yd, packet->zd);
		//LOGI("Client: reading entity with %f, %f, %f\n", e->xd, e->yd, e->zd);
	}

	mpcast(level)->putEntity(packet->entityId, e);
}
void ClientSideNetworkHandler::handle( const RakNet::RakNetGUID& source, AddPaintingPacket* packet ) {
	if (!level)
		return;

	Painting* painting = new Painting(level, packet->xTile, packet->yTile, packet->zTile, packet->dir, packet->motive);
	mpcast(level)->putEntity(packet->entityId, painting);
}
void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, AddMobPacket* packet)
{
	// LOGI("AddMobPacket (%p)\n", level);

	if (!level) {
		LOGW("Trying to add a mob with no level!\n");
		//we skip this since we will get this player anyway when we request level
		return;
	}
	if (!packet->type) {
		LOGE("Trying to add a mob without a type id\n");
		return;
	}

	Mob* mob = MobFactory::CreateMob(packet->type, level);
	if (!mob) {
		LOGE("Server tried to add an unknown mob type! :%d\n", packet->type);
		return;
	}

	mob->entityId = packet->entityId;
	mob->moveTo(packet->x, packet->y, packet->z, packet->yRot, packet->xRot);
	mob->getEntityData()->assignValues(&packet->unpack);
	level->addEntity(mob);
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, AddPlayerPacket* packet)
{
	if (!level) {
		//we skip this since we will get this player anyway when we request level
		return;
	}
	LOGI("AddPlayerPacket\n");

	Player* player = new RemotePlayer(level, minecraft->isCreativeMode());
	minecraft->gameMode->initAbilities(player->abilities);
	player->entityId = packet->entityId;
	level->addEntity(player);

	player->moveTo(packet->x, packet->y, packet->z, packet->yRot, packet->xRot);
	player->name = packet->name.C_String();
	player->owner = packet->owner;
	player->getEntityData()->assignValues(&packet->unpack);
    int slot = Inventory::MAX_SELECTION_SIZE;
    if (packet->carriedItemId == 0) {
        player->inventory->clearSlot(slot);
    } else {
        ItemInstance newItem(packet->carriedItemId, 1, packet->carriedItemAuxValue);
        player->inventory->replaceSlot(slot, &newItem);
    }
    player->inventory->moveToSelectedSlot(slot, true);
	//player->resetPos();

	std::string message = packet->name.C_String();
	message += " joined the game";
#ifndef STANDALONE_SERVER
	minecraft->gui.addMessage(message);
#endif
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, RemovePlayerPacket* packet) {
	if (!level || source == minecraft->player->owner) return;

	if (Player* player = findPlayer(level, packet->entityId, &packet->owner)) {
		player->reallyRemoveIfPlayer = true;
		level->removeEntity(player);
	}

	//for (int i = (int)level->players.size()-1; i >= 0; --i) {
	//	if (level->players[i]->owner == source) {
	//		level->players[i]->reallyRemoveIfPlayer = true;
	//		level->players.erase(level->players.begin() + i);
	//		break;
	//	}
	//}

	//if (!player) return;

	//std::string message = packet->name.C_String();
	//message += " joined the game";
	//minecraft->gui.addMessage(message);
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, RemoveEntityPacket* packet)
{
	if (!level) return;

	Entity* entity = level->getEntity(packet->entityId);
	// LOGI("RemoveEntityPacket %p %p, %d\n", entity, minecraft->player, entity?(int)(entity->isPlayer()): -1);
	if (!entity) return;

	level->removeEntity(entity);
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, AddItemEntityPacket* packet)
{
	if (!level) return;

	ItemEntity* entity = new ItemEntity(level, packet->x, packet->y, packet->z, ItemInstance(packet->itemId, packet->itemCount, packet->auxValue));
	entity->xd = packet->xa();
	entity->yd = packet->ya();
	entity->zd = packet->za();
	//LOGI("item-entity @ client: %f, %f, %f\n", entity->xd, entity->yd, entity->zd);

	mpcast(level)->putEntity(packet->id, entity);
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, TakeItemEntityPacket* packet) {
	// printf("TakeItemEntityPacket \n");
	if (!level) return;

	Entity* e = level->getEntity(packet->itemId);
	if (!e)
		return;
	
	ItemInstance item;
	if (e->isItemEntity()) {
		item = ((ItemEntity*) e)->item;
#ifndef STANDALONE_SERVER
		if (Entity* to = level->getEntity(packet->playerId))
			minecraft->particleEngine->add(new TakeAnimationParticle(level, (ItemEntity*)e, to, -0.5f));
#endif
	}
	else if (e->getEntityTypeId() == EntityTypes::IdArrow)
		item = ItemInstance(Item::arrow);

	if (item.isNull())
		return;

	// try take it and if we don't have space; re-throw it
	if (minecraft->player->entityId == packet->playerId
	&& !minecraft->player->inventory->add(&item)) {
		DropItemPacket dropPacket(packet->playerId, item);
		minecraft->raknetInstance->send(dropPacket);
	}
	level->playSound(e, "random.pop", 0.2f, 1.0f * 2.f);
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, MovePlayerPacket* packet)
{
	if (!level)
		return;

	//printf("MovePlayerPacket\n");
	Entity* entity = level->getEntity(packet->entityId);
	if (entity)
	{
		entity->lerpTo(packet->x, packet->y, packet->z, packet->yRot, packet->xRot, 3);
	}
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, SendInventoryPacket* packet) {
	if (!level) return;

	if (packet->entityId == minecraft->player->entityId) {
		auto items = packet->items;
		
		minecraft->player->inventory->replace(items);

		for (int i = 0; i < packet->linkedSlot.size(); i++) {
			minecraft->player->inventory->linkSlot(i, packet->linkedSlot[i].inventorySlot, true);
			LOGI("%i -> %i\n", packet->linkedSlot[i].inventorySlot, i);
		}
	}
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, TakeItemPacket* packet) {
	if (!level) return;

	LOGI("TakeItemPacket\n");

	ItemInstance* item = new ItemInstance();

	item->count = packet->count;
	item->id = packet->itemId;
	item->setAuxValue(packet->auxValue);

	// if (minecraft->player->entityId == packet->playerId) {
	if (!minecraft->player->inventory->add(item)) {
		minecraft->player->drop(new ItemInstance(*item), false);
	}
	// }
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, RemoveItemPacket* packet) {
	// Idk how it works...
	if (!level) return;

	ItemInstance item;

	item.count = packet->count;
	item.id = packet->itemId;
	item.setAuxValue(packet->auxValue);

	// if (minecraft->player->entityId == packet->playerId) {
	minecraft->player->inventory->removeResource(item);
	// }
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, MoveEntityPacket* packet)
{
	if (!level)
		return;

	//printf("MovePlayerPacket\n");
	Entity* entity = level->getEntity(packet->entityId);
	if (entity)
	{
		float xRot = packet->hasRot? packet->xRot : entity->xRot;
		float yRot = packet->hasRot? packet->yRot : entity->yRot;
		entity->lerpTo(packet->x, packet->y, packet->z, yRot, xRot, 3);
	}
}

void ClientSideNetworkHandler::handle(
	const RakNet::RakNetGUID& source,
	UpdateBlockPacket* packet)
{
	if (!level)
		return;

	const int x = packet->x;
	const int y = static_cast<int>(packet->y);
	const int z = packet->z;

	if (x < 0 || x >= LevelConstants::LEVEL_WIDTH ||
		z < 0 || z >= LevelConstants::LEVEL_DEPTH ||
		y < 0 || y >= LevelConstants::LEVEL_HEIGHT)
	{
		LOGI("Ignoring out-of-world block update (%d, %d, %d)\n",
			x, y, z);
		return;
	}

	const int chunkX = x / LevelConstants::CHUNK_WIDTH;
	const int chunkZ = z / LevelConstants::CHUNK_DEPTH;

	const int tileId =
		Tile::transformToValidBlockId(packet->blockId, x, y, z);

	if (isChunkLoaded(chunkX, chunkZ))
	{
		// This is an authoritative update from the server.
		// Do not schedule a rollback to the previous block.
		mpcast(level)->doSetTileAndData(
			x, y, z, tileId, packet->blockData);
	}
	else
	{
		// Remember the update until its chunk has arrived.
		SBufferedBlockUpdate update;
		update.blockId = packet->blockId;
		update.blockData = packet->blockData;
		update.setData = true;
		update.x = x;
		update.y = packet->y;
		update.z = z;

		bufferedBlockUpdates.push_back(update);
	}
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, ExplodePacket* packet) {
	if (!level) return;
	Explosion explosion(level, NULL, packet->x, packet->y, packet->z, packet->r);
	explosion.toBlow.insert(packet->toBlow.begin(), packet->toBlow.end());
	explosion.finalizeExplosion();
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, LevelEventPacket* packet) {
	if (!level) return;
	if(packet->eventId == LevelEvent::ALL_PLAYERS_SLEEPING) {
		minecraft->player->setAllPlayersSleeping();
	}
	else {
		minecraft->level->levelEvent(NULL, packet->eventId, packet->x, packet->y, packet->z, packet->data);
	}
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, TileEventPacket* packet) {
	if (!level) return;
	minecraft->level->tileEvent(packet->x, packet->y, packet->z, packet->b0, packet->b1);
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, EntityEventPacket* packet) {
	if (!level) return;

	Entity* e = level->getEntity(packet->entityId);
	if (e) e->handleEntityEvent(packet->eventId);
}

void ClientSideNetworkHandler::handle(
	const RakNet::RakNetGUID& source,
	ChunkDataPacket* packet)
{
	if (!level || !level->getChunkSource())
	{
		LOGI("Cannot process ChunkDataPacket: level or chunk source is NULL\n");
		return;
	}

	const int width = LevelConstants::CHUNK_CACHE_WIDTH;

	// Never accept a packet outside the world's actual chunk grid.
	if (packet->x < 0 || packet->x >= width ||
		packet->z < 0 || packet->z >= width)
	{
		LOGI("Ignoring out-of-world ChunkDataPacket (%d, %d)\n",
			packet->x, packet->z);
		return;
	}

	LevelChunk* chunk =
		level->getChunkSource()->create(packet->x, packet->z);

	if (!chunk || chunk->isEmpty() ||
		chunk->x != packet->x || chunk->z != packet->z)
	{
		LOGI("Failed to find writable chunk (%d, %d)\n",
			packet->x, packet->z);
		return;
	}

	DataLayer& blockData = chunk->data;

	const int setSize = LevelConstants::LEVEL_HEIGHT / 8;
	const int setShift = 4;

	bool recalcHeight = false;

	int x0 = 16, x1 = 0;
	int z0 = 16, z1 = 0;
	int y0 = LevelConstants::LEVEL_HEIGHT, y1 = 0;

	const int rx = packet->x << 4;
	const int rz = packet->z << 4;

	unsigned char* readBlockBuffer =
		new unsigned char[setSize];

	unsigned char* readDataBuffer =
		new unsigned char[setSize / 2];

	unsigned char* blockIds = chunk->getBlockData();

	MultiPlayerLevel* multiplayerLevel = mpcast(level);

	for (int i = 0; i < LevelConstants::CHUNK_COLUMNS; ++i)
	{
		unsigned char updateBits = 0;

		packet->chunkData.Read(updateBits);

		if (updateBits == 0)
			continue;

		recalcHeight = true;

		const int colX = i % LevelConstants::CHUNK_WIDTH;
		const int colZ = i / LevelConstants::CHUNK_WIDTH;

		const int colDataPosition =
			(colX << 11) | (colZ << 7);

		for (int set = 0; set < 8; ++set)
		{
			if ((updateBits & (1 << set)) != 0)
			{
				packet->chunkData.Read(
					reinterpret_cast<char*>(readBlockBuffer),
					setSize);

				packet->chunkData.Read(
					reinterpret_cast<char*>(readDataBuffer),
					setSize / 2);

				for (int part = 0; part < setSize; ++part)
				{
					const int x = rx + colX;
					const int y = (set << setShift) + part;
					const int z = rz + colZ;

					const int tileId =
						Tile::transformToValidBlockId(
							readBlockBuffer[part], x, y, z);

					// Apply server data without creating a rollback entry.
					multiplayerLevel->doSetTileNoUpdate(
						x, y, z, tileId);
				}

				std::memcpy(
					&blockData.data[
						(colDataPosition + (set << setShift)) >> 1],
					readDataBuffer,
							setSize >> 1);
			}

			const int sectionY = (1 << set) << setShift;

			if (sectionY < y0)
				y0 = sectionY;

			if (sectionY + setSize - 1 > y1)
				y1 = sectionY + setSize - 1;
		}

		if (colX < x0)
			x0 = colX;

		if (colX > x1)
			x1 = colX;

		if (colZ < z0)
			z0 = colZ;

		if (colZ > z1)
			z1 = colZ;
	}

	delete[] readBlockBuffer;
	delete[] readDataBuffer;

	if (recalcHeight)
	{
		level->setTilesDirty(
			rx + x0, y0, rz + z0,
			rx + x1, y1, rz + z1);
	}

	// The received server snapshot itself is not a local edit.
	chunk->unsaved = false;

	const int loadedIndex = packet->x * width + packet->z;

	if (loadedIndex < 0 ||
		loadedIndex >= static_cast<int>(chunksLoaded.size()))
	{
		LOGI("Chunk loaded index out of bounds (%d, %d)\n",
			packet->x, packet->z);
		return;
	}

	// Count each chunk only once.
	if (!chunksLoaded[loadedIndex])
	{
		chunksLoaded[loadedIndex] = true;
		++loadedChunkCount;
	}

	// Apply buffered updates as soon as their chunk arrives.
	// Do not leave them waiting for the entire world to load.
	for (BlockUpdateList::iterator it = bufferedBlockUpdates.begin();
		it != bufferedBlockUpdates.end();)
	{
		const int updateChunkX =
			it->x / LevelConstants::CHUNK_WIDTH;

		const int updateChunkZ =
			it->z / LevelConstants::CHUNK_DEPTH;

		if (updateChunkX == packet->x &&
			updateChunkZ == packet->z)
		{
			const int updateY = static_cast<int>(it->y);

			const int tileId =
				Tile::transformToValidBlockId(
					it->blockId,
					it->x,
					updateY,
					it->z);

			if (it->setData)
			{
				multiplayerLevel->doSetTileAndData(
					it->x,
					updateY,
					it->z,
					tileId,
					it->blockData);
			}
			else
			{
				multiplayerLevel->doSetTileNoUpdate(
					it->x,
					updateY,
					it->z,
					tileId);
			}

			it = bufferedBlockUpdates.erase(it);
		}
		else
		{
			++it;
		}
	}

	// Let the guest start playing once their actual spawn chunk arrives.
	if (minecraft && minecraft->player)
	{
		LocalPlayer* localPlayer =
			static_cast<LocalPlayer*>(minecraft->player);

		const int playerChunkX = Mth::floor(
			localPlayer->x / (float)LevelConstants::CHUNK_WIDTH);

		const int playerChunkZ = Mth::floor(
			localPlayer->z / (float)LevelConstants::CHUNK_DEPTH);

		if (packet->x == playerChunkX &&
			packet->z == playerChunkZ)
		{
			localPlayer->waitingForInitialChunk = false;

			LOGI("Initial player chunk received (%d, %d)\n",
				packet->x, packet->z);
		}
	}

	if (areAllChunksLoaded())
	{
		ReadyPacket readyPacket(
			ReadyPacket::READY_REQUESTEDCHUNKS);

		raknetInstance->send(readyPacket);
	}
	else
	{
		requestNextChunk();
	}
}

class _ChunkSorter
{
public:
    _ChunkSorter(int x, int z)
    :   x(x),
        y(z)
    {
    }
    
    bool operator() (const IntPair& a, const IntPair& b) {
        const int ax = a.x - x, ay = a.y - y;
        const int bx = b.x - x, by = b.y - y;
        return (ax*ax + ay*ay) < (bx*bx + by*by);
    }
private:
    int x;
    int y;
};


void ClientSideNetworkHandler::arrangeRequestChunkOrder() {
	clearChunksLoaded();

    // Default sort is around center of the world
    int cx = LevelConstants::CHUNK_CACHE_WIDTH / 2;
    int cz = LevelConstants::CHUNK_CACHE_WIDTH / 2;

    // If player exists, let's sort around him
    Player* p = minecraft? minecraft->player : NULL;
    if (p) {
        cx = Mth::floor(p->x / (float)LevelConstants::CHUNK_WIDTH);
        cz = Mth::floor(p->z / (float)LevelConstants::CHUNK_DEPTH);
    }

    _ChunkSorter sorter(cx, cz);
    std::sort(requestNextChunkIndexList.begin(), requestNextChunkIndexList.end(), sorter);
}

void ClientSideNetworkHandler::levelGenerated(Level* level)
{
	this->level = level;
	ReadyPacket packet(ReadyPacket::READY_CLIENTGENERATION);
	raknetInstance->send(packet);

    arrangeRequestChunkOrder();
	requestNextChunk();
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, PlayerEquipmentPacket* packet)
{
	if (!level)
		return;

	Entity* entity = level->getEntity(packet->entityId);
	if (!entity || !entity->isPlayer())
		return;

	Player* player = (Player*)entity;
	// make sure it's not our local player
	if (player->owner == rakPeer->GetMyGUID())
	{
		printf("Attempted to modify local player's inventory\n");
		return;
	}
	// override the player's inventory
	//int slot = player->inventory->getSlot(packet->itemId, packet->itemAuxValue);
	//if (slot >= 0) {
	//	player->inventory->moveToSelectedSlot(slot, true);
		//item->id = packet->itemId;
		//item->setAuxValue(packet->itemAuxValue);
		//item->count = 63;
	int slot = Inventory::MAX_SELECTION_SIZE;
	if (slot >= 0) {
		if (packet->itemId == 0) {
			player->inventory->clearSlot(slot);
		} else {
			ItemInstance newItem(packet->itemId, 63, packet->itemAuxValue);
			player->inventory->replaceSlot(slot, &newItem);
		}
		player->inventory->moveToSelectedSlot(slot, true);
	} else {
		LOGW("Warning: Remote player doesn't have his thing, Odd!\n");
	}
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, PlayerArmorEquipmentPacket* packet) {
	if (!level)
		return;

	Entity* entity = level->getEntity(packet->entityId);
	if (!entity || !entity->isPlayer())
		return;

	Player* player = (Player*)entity;
	// make sure it's not our local player, since that should be UpdateArmorPacket
	if (player->owner == rakPeer->GetMyGUID()) {
		printf("Attempted to modify local player's armor visually\n");
		return;
	}

	packet->fillIn(player);
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, InteractPacket* packet)
{
	if (!level)
		return;

	Entity* src = level->getEntity(packet->sourceId);
	Entity* entity = level->getEntity(packet->targetId);
	if (src && entity && src->isPlayer())
	{
		Player* player = (Player*) src;
		if (InteractPacket::Attack == packet->action)
			minecraft->gameMode->attack(player, entity);
		if (InteractPacket::Interact == packet->action)
			minecraft->gameMode->interact(player, entity);
	}
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, SetEntityDataPacket* packet)
{
	if (!level)
		return;

	// LOGI("SetEntityDataPacket\n");

	Entity* e = level->getEntity(packet->id);
	if (e) {
		SynchedEntityData* data = e->getEntityData();
		if (data)
			data->assignValues(&packet->getUnpackedData());
	}
}

void ClientSideNetworkHandler::handle( const RakNet::RakNetGUID& source, SetEntityMotionPacket* packet )
{
	if (!level)
		return;

	if (Entity* e = level->getEntity(packet->id)) {
		/*
		e->xd = packet->xd;
		e->yd = packet->yd;
		e->zd = packet->zd;
		*/
		e->lerpMotion(packet->xd, packet->yd, packet->zd);
	}
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, AnimatePacket* packet)
{
	if (!level)
		return;

    // Own player - Then don't play... :
    if (minecraft->player->entityId == packet->entityId) {
        if (packet->action == AnimatePacket::Swing) return;
    }

	Entity* entity = level->getEntity(packet->entityId);
	if (!entity || !entity->isPlayer())
        return;
    
	Player* player = (Player*) entity;

    switch (packet->action) {
	case AnimatePacket::Swing:
		player->swing();
		break;
	case AnimatePacket::WAKE_UP:
		player->stopSleepInBed(false, false, false);
		break;
	default:
		LOGW("Unknown Animate action: %d\n", packet->action);
		break;
	}
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, WantCreatePacket* packet)
{
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, UseItemPacket* packet)
{
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, SetHealthPacket* packet)
{
	if (!level || !minecraft->player)
		return;

	printf("SetHealthPacket \n");
	minecraft->player->hurtTo(packet->health);
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, SetSpawnPositionPacket* packet) {
	if (!level || !minecraft || !minecraft->player) return;
	if (!level->inRange(packet->x, packet->y, packet->z)) return;

	minecraft->player->setRespawnPosition(Pos(packet->x, packet->y, packet->z));
	level->getLevelData()->setSpawn(packet->x, packet->y, packet->z);
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, HurtArmorPacket* packet) {
    if (!level || !minecraft->player) {
        return;
    }

    minecraft->player->hurtArmor(packet->dmg);
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, RespawnPacket* packet)
{
	if (level) {
		//LOGI("RespawnPacket! %d\n", findPlayer(level, packet->entityId, NULL));
		NetEventCallback::handle(level, source, packet );
	}
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, ContainerOpenPacket* packet)
{
	if (!level || !minecraft || !minecraft->player)
		return;

	if (packet->type == ContainerType::FURNACE) {
		FurnaceTileEntity* te = new FurnaceTileEntity();
		te->clientSideOnly = true;
		minecraft->player->openFurnace(te);
		if (minecraft->player->containerMenu)
			minecraft->player->containerMenu->containerId = packet->containerId;
	}
	if (packet->type == ContainerType::CONTAINER) {
		ChestTileEntity* te = new ChestTileEntity(packet->size);
		te->clientSideOnly = true;
		minecraft->player->openContainer(te);
		if (minecraft->player->containerMenu)
			minecraft->player->containerMenu->containerId = packet->containerId;
	}
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, ContainerClosePacket* packet)
{
	if (minecraft && minecraft->player && minecraft->player->containerMenu)
		minecraft->player->closeContainer();
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, ContainerSetContentPacket* packet)
{
	if (!minecraft || !minecraft->player)
		return;

    if (packet->containerId == 0) {
        for (unsigned int i = 0; i < packet->items.size(); ++i) {
            minecraft->player->inventory->setItem(Inventory::MAX_SELECTION_SIZE + i, &packet->items[i]);
        }
    } else if (minecraft->player->containerMenu && minecraft->player->containerMenu->containerId == packet->containerId) {
        for (unsigned int i = 0; i < packet->items.size(); ++i) {
            minecraft->player->containerMenu->setSlot(i, &packet->items[i]);
        }
    }
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, ContainerSetSlotPacket* packet)
{
	//LOGI("ContainerSetSlot\n");

	if (!minecraft->player
	  || !minecraft->player->containerMenu
	  || minecraft->player->containerMenu->containerId != packet->containerId)
	  return;

	//minecraft->player->containerMenu->setSlot(packet->slot, packet->item.isNull()? NULL : &packet->item);
	minecraft->player->containerMenu->setSlot(packet->slot, &packet->item);
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, ContainerSetDataPacket* packet)
{
	//LOGI("ContainerSetData\n");
	if (minecraft->player && minecraft->player->containerMenu && minecraft->player->containerMenu->containerId == packet->containerId) {
		//LOGI("client: SetData2 %d, %d\n", packet->id, packet->value);
		minecraft->player->containerMenu->setData(packet->id, packet->value);
	}
}

void ClientSideNetworkHandler::handle( const RakNet::RakNetGUID& source, ChatPacket* packet )
{
#ifndef STANDALONE_SERVER
	// minecraft->gui.displayClientMessage(packet->message);
	std::istringstream iss(packet->message);
	for (std::string line; std::getline(iss, line); ) {
		minecraft->gui.addMessage(line);
	}
#endif
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, SignUpdatePacket* packet)
{
	if (!level)
		return;

	TileEntity* te = level->getTileEntity(packet->x, packet->y, packet->z);
	if (TileEntity::isType(te, TileEntityType::Sign)) {
		SignTileEntity* ste = (SignTileEntity*) te;
		if (ste->isEditable()) {
			for (int i = 0; i < SignTileEntity::NUM_LINES; i++) {
				ste->messages[i] = packet->lines[i];
			}
			//ste->setChanged();
		}
	}
}

void ClientSideNetworkHandler::handle(const RakNet::RakNetGUID& source, AdventureSettingsPacket* packet) {
    if (!level)
		return;
	//assert(level != NULL && "level is NULL @ handle(AdventureSettingsPacket*)");

    packet->fillIn(level->adventureSettings);
}

void ClientSideNetworkHandler::clearChunksLoaded()
{
	const int width = LevelConstants::CHUNK_CACHE_WIDTH;

	NumRequestChunks = width * width;

	requestNextChunkPosition = 0;
	requestNextChunkIndex = 0;
	loadedChunkCount = 0;

	requestNextChunkIndexList.resize(NumRequestChunks);
	chunksLoaded.assign(NumRequestChunks, false);

	// Enumerate every chunk in the actual world.
	for (int i = 0; i < NumRequestChunks; ++i)
	{
		requestNextChunkIndexList[i].x = i / width;
		requestNextChunkIndexList[i].y = i % width;
	}

	// Updates from an earlier connection must not leak into a new one.
	bufferedBlockUpdates.clear();
}
