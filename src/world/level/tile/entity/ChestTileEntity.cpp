#include "ChestTileEntity.h"
#include "../ChestTile.h"
#include "../../Level.h"
#include "../../../entity/player/Player.h"
#include "../../../../nbt/NbtIo.h"

ChestTileEntity::ChestTileEntity()
	: ChestTileEntity(ItemsSize)
{
}

ChestTileEntity::ChestTileEntity(int containerSize)
	: super(TileEntityType::Chest),
	FillingContainer(
		containerSize, 0, ContainerType::CONTAINER, false),
	tickInterval(0),
	openCount(0),
	openness(0), oOpenness(0),
	hasCheckedNeighbors(false),
	n(NULL), s(NULL), w(NULL), e(NULL)
{
}

ChestTileEntity* ChestTileEntity::getAdjacentChest(
	int dx, int dz) const
{
	if (!level)
		return NULL;

	if (level->getTile(x + dx, y, z + dz) != Tile::chest->id)
		return NULL;

	TileEntity* te = level->getTileEntity(
		x + dx, y, z + dz);

	if (!TileEntity::isType(te, TileEntityType::Chest))
		return NULL;

	return (ChestTileEntity*)te;
}

ChestTileEntity* ChestTileEntity::getChestForSlot(
	int slot, int& localSlot) const
{
	// Multiplayer GUI inventories are temporary containers.
	// Their 54 slots already live inside one local object.
	if (numTotalSlots > ItemsSize) {
		if (slot < 0 || slot >= numTotalSlots)
			return NULL;

		localSlot = slot;
		return const_cast<ChestTileEntity*>(this);
	}

	if (slot < 0)
		return NULL;

	ChestTileEntity* west = getAdjacentChest(-1, 0);
	ChestTileEntity* north = getAdjacentChest(0, -1);
	ChestTileEntity* east = getAdjacentChest(1, 0);
	ChestTileEntity* south = getAdjacentChest(0, 1);

	ChestTileEntity* first = const_cast<ChestTileEntity*>(this);
	ChestTileEntity* second = NULL;

	// Keep slot ordering consistent regardless of which half
	// was clicked: west first, or north first.
	if (west) {
		first = west;
		second = const_cast<ChestTileEntity*>(this);
	}
	else if (north) {
		first = north;
		second = const_cast<ChestTileEntity*>(this);
	}
	else if (east) {
		second = east;
	}
	else if (south) {
		second = south;
	}

	if (slot < ItemsSize) {
		localSlot = slot;
		return first;
	}

	if (slot < ItemsSize * 2 && second) {
		localSlot = slot - ItemsSize;
		return second;
	}

	return NULL;
}

int ChestTileEntity::getContainerSize() const
{
	// Temporary multiplayer inventory.
	if (numTotalSlots > ItemsSize)
		return numTotalSlots;

	int localSlot = 0;

	return getChestForSlot(ItemsSize, localSlot)
		? ItemsSize * 2
		: ItemsSize;
}

ItemInstance* ChestTileEntity::getItem(int slot)
{
	int localSlot = 0;
	ChestTileEntity* owner =
		getChestForSlot(slot, localSlot);

	return owner
		? owner->FillingContainer::getItem(localSlot)
		: NULL;
}

void ChestTileEntity::setItem(int slot, ItemInstance* item)
{
	int localSlot = 0;
	ChestTileEntity* owner =
		getChestForSlot(slot, localSlot);

	if (!owner)
		return;

	owner->FillingContainer::setItem(localSlot, item);

	ItemInstance* stored =
		owner->FillingContainer::getItem(localSlot);

	if (stored && stored->count > owner->getMaxStackSize())
		stored->count = owner->getMaxStackSize();

	if (owner->level)
		owner->setChanged();
}

void ChestTileEntity::clearSlot(int slot)
{
	int localSlot = 0;
	ChestTileEntity* owner =
		getChestForSlot(slot, localSlot);

	if (!owner)
		return;

	owner->FillingContainer::clearSlot(localSlot);

	if (owner->level)
		owner->setChanged();
}

bool ChestTileEntity::add(ItemInstance* item)
{
	if (!item || item->isNull())
		return true;

	const int originalCount = item->count;
	const int size = getContainerSize();

	// Fill existing compatible stacks first.
	if (item->isStackable()) {
		for (int i = 0; i < size && item->count > 0; ++i) {
			ItemInstance* existing = getItem(i);

			if (!existing || existing->isNull())
				continue;

			if (existing->id != item->id
				|| !existing->isStackable())
				continue;

			if (existing->isStackedByData()
				&& existing->getAuxValue() != item->getAuxValue())
				continue;

			int maxStack = existing->getMaxStackSize();
			if (maxStack > getMaxStackSize())
				maxStack = getMaxStackSize();

			int space = maxStack - existing->count;
			if (space <= 0)
				continue;

			int moved = item->count < space
				? item->count : space;

			existing->count += moved;
			item->count -= moved;

			int localSlot = 0;
			ChestTileEntity* owner =
				getChestForSlot(i, localSlot);

			if (owner && owner->level)
				owner->setChanged();
		}
	}

	// Then fill empty slots across both halves.
	for (int i = 0; i < size && item->count > 0; ++i) {
		ItemInstance* existing = getItem(i);

		if (existing && !existing->isNull())
			continue;

		int maxStack = item->getMaxStackSize();
		if (maxStack > getMaxStackSize())
			maxStack = getMaxStackSize();

		int moved = item->count < maxStack
			? item->count : maxStack;

		ItemInstance stack(*item);
		stack.count = moved;
		setItem(i, &stack);

		item->count -= moved;
	}

	return item->count < originalCount;
}

/*
ItemInstance ChestTileEntity::removeItem( int slot, int count )
{
	if (!items[slot].isNull()) {
		if (items[slot].count <= count) {
			ItemInstance item = items[slot];
			items[slot].setNull();
			this->setChanged();
			return item;
		} else {
			ItemInstance i = items[slot].remove(count);
			if (items[slot].count == 0) items[slot].setNull();
			this->setChanged();
			return i;
		}
	}
	return ItemInstance();
}

ItemInstance ChestTileEntity::removeItemNoUpdate( int slot )
{
	if (!items[slot].isNull()) {
		ItemInstance item = items[slot];
		items[slot].setNull();
		return item;
	}
	return ItemInstance();
}

void ChestTileEntity::setItem( int slot, ItemInstance* item )
{
	items[slot] = item? *item : ItemInstance();
	if (item != NULL && item->count > getMaxStackSize()) item->count = getMaxStackSize();
	this->setChanged();
}
*/

std::string ChestTileEntity::getName() const
{
	return "container.chest";
}

bool ChestTileEntity::shouldSave() {
	for (int i = 0; i < ItemsSize; ++i)
		if (items[i] && !items[i]->isNull()) return true;
	return false;
}

void ChestTileEntity::load( CompoundTag* base )
{
	super::load(base);

	if (!base->contains("Items"))//, Tag::TAG_List)
		return;

	ListTag* inventoryList = base->getList("Items");
	for (int i = 0; i < inventoryList->size(); i++) {
		Tag* ttag = inventoryList->get(i);
		if (ttag->getId() != Tag::TAG_Compound)
			continue;

		CompoundTag* tag = (CompoundTag*) ttag;
		int slot = tag->getByte("Slot") & 0xff;
		if (slot >= 0 && slot < ItemsSize) {
			if (!items[slot]) items[slot] = new ItemInstance(); //@chestodo
			items[slot]->load(tag);
		}
	}
}

bool ChestTileEntity::save( CompoundTag* base )
{
	if (!super::save(base))
		return false;

	ListTag* listTag = new ListTag();

	for (int i = 0; i < ItemsSize; i++) {
		if (items[i] && !items[i]->isNull()) {
			CompoundTag* tag = new CompoundTag();
			tag->putByte("Slot", (char) i);
			items[i]->save(tag);
			listTag->add(tag);
		}
	}
	base->put("Items", listTag);
	return true;
}

int ChestTileEntity::getMaxStackSize() const
{
	return Container::LARGE_MAX_STACK_SIZE;
}

bool ChestTileEntity::stillValid( Player* player )
{
	if (level->getTileEntity(x, y, z) != this) return false;
	if (player->distanceToSqr(x + 0.5f, y + 0.5f, z + 0.5f) > 8 * 8) return false;
	return true;
}

void ChestTileEntity::clearCache()
{
	super::clearCache();
	hasCheckedNeighbors = false;
}

void ChestTileEntity::checkNeighbors()
{
	if (hasCheckedNeighbors)
		return;

	hasCheckedNeighbors = true;
	n = NULL;
	e = NULL;
	w = NULL;
	s = NULL;

	//        if (getTile() != NULL) {
	if (level->getTile(x - 1, y, z) == Tile::chest->id) {
		w = (ChestTileEntity*) level->getTileEntity(x - 1, y, z);
	}
	if (level->getTile(x + 1, y, z) == Tile::chest->id) {
		e = (ChestTileEntity*) level->getTileEntity(x + 1, y, z);
	}
	if (level->getTile(x, y, z - 1) == Tile::chest->id) {
		n = (ChestTileEntity*) level->getTileEntity(x, y, z - 1);
	}
	if (level->getTile(x, y, z + 1) == Tile::chest->id) {
		s = (ChestTileEntity*) level->getTileEntity(x, y, z + 1);
	}

}

void ChestTileEntity::tick()
{
	super::tick();
	checkNeighbors();

	if (++tickInterval >= 4 * SharedConstants::TicksPerSecond) {
		level->tileEvent(x, y, z, ChestTile::EVENT_SET_OPEN_COUNT, openCount);
		tickInterval = 0;
	}

	oOpenness = openness;

	float speed = 0.10f;
	if (openCount > 0 && openness == 0) {
		if (n == NULL && w == NULL) {
			float xc = x + 0.5f;
			float zc = z + 0.5f;
			if (s != NULL) zc += 0.5f;
			if (e != NULL) xc += 0.5f;

			level->playSound(xc, y + 0.5f, zc, "random.chestopen", 0.5f, level->random.nextFloat() * 0.1f + 0.9f);
		}
	}
	if ((openCount == 0 && openness > 0) || (openCount > 0 && openness < 1)) {
		float oldOpen = openness;
		if (openCount > 0) openness += speed;
		else openness -= speed;
		if (openness > 1) {
			openness = 1;
		}
		float lim = 0.5f;
		if (openness < lim && oldOpen >= lim) {
			if (n == NULL && w == NULL) {
				float xc = x + 0.5f;
				float zc = z + 0.5f;
				if (s != NULL) zc += 0.5f;
				if (e != NULL) xc += 0.5f;

				level->playSound(xc, y + 0.5f, zc, "random.chestclosed", 0.5f, level->random.nextFloat() * 0.1f + 0.9f);
			}
		}
		if (openness < 0) {
			openness = 0;
		}
	}
}

void ChestTileEntity::triggerEvent( int b0, int b1 )
{
	if (b0 == ChestTile::EVENT_SET_OPEN_COUNT) {
		openCount = b1;
	}
}

void ChestTileEntity::startOpen()
{
	openCount++;
	level->tileEvent(x, y, z, ChestTile::EVENT_SET_OPEN_COUNT, openCount);
}

void ChestTileEntity::stopOpen()
{
	openCount--;
	level->tileEvent(x, y, z, ChestTile::EVENT_SET_OPEN_COUNT, openCount);
}

void ChestTileEntity::setRemoved()
{
	clearCache();
	checkNeighbors();
	super::setRemoved();
}
