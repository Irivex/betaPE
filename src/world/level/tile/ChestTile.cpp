#include "ChestTile.h"
#include "entity/ChestTileEntity.h"
#include "../Level.h"
#include "../material/Material.h"
#include "../../Facing.h"
#include "../../entity/item/ItemEntity.h"

ChestTile::ChestTile( int id )
:	super(id, Material::wood)
{
	tex = 10 + 16;
	const float m = 0.025f;
	setShape(m, 0, m, 1-m, 1-m-m, 1-m);
}

bool ChestTile::isSolidRender()
{
	return false;
}

bool ChestTile::isCubeShaped()
{
	return false;
}

int ChestTile::getRenderShape()
{
	return super::getRenderShape();
	//return Tile::SHAPE_ENTITYTILE_ANIMATED;
}

void ChestTile::updateShape(
	LevelSource* level, int x, int y, int z)
{
	const float margin = 0.025f;

	// Detect adjacent chests in every horizontal direction.
	const bool west = level->getTile(x - 1, y, z) == id;
	const bool east = level->getTile(x + 1, y, z) == id;
	const bool north = level->getTile(x, y, z - 1) == id;
	const bool south = level->getTile(x, y, z + 1) == id;

	// Keep outer edges inset, but eliminate margins
	// on sides that touch another chest.
	const float x0 = west ? 0.0f : margin;
	const float x1 = east ? 1.0f : 1.0f - margin;
	const float z0 = north ? 0.0f : margin;
	const float z1 = south ? 1.0f : 1.0f - margin;

	Tile::setShape(
		x0, 0.0f, z0,
		x1, 1.0f - margin - margin, z1
	);
}

void ChestTile::onPlace(Level* level, int x, int y, int z)
{
	super::onPlace(level, x, y, z);
	recalcLockDir(level, x, y, z);

	const int offsetX[] = { -1, 1, 0, 0 };
	const int offsetZ[] = { 0, 0, -1, 1 };

	// Refresh adjacent chest entities when a pair is formed.
	for (int i = 0; i < 4; ++i) {
		int nx = x + offsetX[i];
		int nz = z + offsetZ[i];

		if (level->getTile(nx, y, nz) != id)
			continue;

		TileEntity* te = level->getTileEntity(nx, y, nz);
		if (te)
			te->clearCache();

		recalcLockDir(level, nx, y, nz);
	}
}

void ChestTile::setPlacedBy(
	Level* level, int x, int y, int z, Mob* by)
{
	// Calculate the normal chest facing from the player's rotation.
	int dir = (Mth::floor(by->yRot * 4 / 360.0f + 0.5f)) & 3;

	int facing = Facing::NORTH;

	if (dir == 0) facing = Facing::NORTH;
	if (dir == 1) facing = Facing::EAST;
	if (dir == 2) facing = Facing::SOUTH;
	if (dir == 3) facing = Facing::WEST;

	// Find an existing chest to pair with.
	int partnerX = x;
	int partnerZ = z;

	if (level->getTile(x, y, z - 1) == id) {
		partnerZ = z - 1;
	}
	else if (level->getTile(x, y, z + 1) == id) {
		partnerZ = z + 1;
	}
	else if (level->getTile(x - 1, y, z) == id) {
		partnerX = x - 1;
	}
	else if (level->getTile(x + 1, y, z) == id) {
		partnerX = x + 1;
	}

	const bool hasPartner =
		partnerX != x || partnerZ != z;

	if (hasPartner) {
		// Preserve the existing chest's orientation.
		const int partnerFacing =
			level->getData(partnerX, y, partnerZ);

		if (partnerFacing >= Facing::NORTH
			&& partnerFacing <= Facing::EAST) {
			facing = partnerFacing;
		}
	}

	// Always assign valid facing metadata to the new chest.
	level->setData(x, y, z, facing);
}

void ChestTile::recalcLockDir(Level* level, int x, int y, int z)
{
	if (level->isClientSide)
		return;

	const int n = level->getTile(x, y, z - 1);
	const int s = level->getTile(x, y, z + 1);
	const int w = level->getTile(x - 1, y, z);
	const int e = level->getTile(x + 1, y, z);

	int lockDir = level->getData(x, y, z);

	// A neighbouring chest is a valid partner, not an obstacle.
	const bool solidNorth = n != id && Tile::solid[n];
	const bool solidSouth = s != id && Tile::solid[s];
	const bool solidWest = w != id && Tile::solid[w];
	const bool solidEast = e != id && Tile::solid[e];

	// Preserve existing behaviour around ordinary solid blocks.
	if ((lockDir == Facing::NORTH && solidNorth)
		|| (lockDir == Facing::SOUTH && solidSouth)
		|| (lockDir == Facing::WEST && solidWest)
		|| (lockDir == Facing::EAST && solidEast)) {

		if (solidNorth && !solidSouth)
			lockDir = Facing::SOUTH;

		if (solidSouth && !solidNorth)
			lockDir = Facing::NORTH;

		if (solidWest && !solidEast)
			lockDir = Facing::EAST;

		if (solidEast && !solidWest)
			lockDir = Facing::WEST;
	}

	level->setData(x, y, z, lockDir);
}

int ChestTile::getTexture(
	LevelSource* level, int x, int y, int z, int face)
{
	// Top and bottom.
	if (face == 1 || face == 0)
		return tex - 1;

	const int n = level->getTile(x, y, z - 1);
	const int s = level->getTile(x, y, z + 1);
	const int w = level->getTile(x - 1, y, z);
	const int e = level->getTile(x + 1, y, z);

	// Chest pair running north/south.
	if (n == id || s == id) {
		if (face == 2 || face == 3)
			return tex;

		int offs = 0;
		if (n == id)
			offs = -1;

		const int w2 = level->getTile(
			x - 1, y, n == id ? z - 1 : z + 1);
		const int e2 = level->getTile(
			x + 1, y, n == id ? z - 1 : z + 1);

		if (face == 4)
			offs = -1 - offs;

		int lockDir = level->getData(x, y, z);

		// A north/south pair must face east or west.
		if (lockDir != Facing::EAST && lockDir != Facing::WEST)
			lockDir = Facing::EAST;

		// If only one side is obstructed, face away from it.
		if ((Tile::solid[w] || Tile::solid[w2])
			&& !Tile::solid[e] && !Tile::solid[e2])
			lockDir = Facing::EAST;

		if ((Tile::solid[e] || Tile::solid[e2])
			&& !Tile::solid[w] && !Tile::solid[w2])
			lockDir = Facing::WEST;

		return (face == lockDir ? tex + 16 : tex + 32)
			+ offs;
	}

	// Chest pair running west/east.
	if (w == id || e == id) {
		if (face == 4 || face == 5)
			return tex;

		int offs = 0;
		if (w == id)
			offs = -1;

		const int n2 = level->getTile(
			w == id ? x - 1 : x + 1, y, z - 1);
		const int s2 = level->getTile(
			w == id ? x - 1 : x + 1, y, z + 1);

		if (face == 3)
			offs = -1 - offs;

		int lockDir = level->getData(x, y, z);

		// A west/east pair must face north or south.
		if (lockDir != Facing::NORTH && lockDir != Facing::SOUTH)
			lockDir = Facing::SOUTH;

		// If only one side is obstructed, face away from it.
		if ((Tile::solid[n] || Tile::solid[n2])
			&& !Tile::solid[s] && !Tile::solid[s2])
			lockDir = Facing::SOUTH;

		if ((Tile::solid[s] || Tile::solid[s2])
			&& !Tile::solid[n] && !Tile::solid[n2])
			lockDir = Facing::NORTH;

		return (face == lockDir ? tex + 16 : tex + 32)
			+ offs;
	}

	// Ordinary single chest.
	const int lockDir = level->getData(x, y, z);

	return face == lockDir ? tex + 1 : tex;
}

int ChestTile::getTexture( int face )
{
	if (face == 1) return tex - 1;
	if (face == 0) return tex - 1;
	if (face == 3) return tex + 1;
	return tex;
}

bool ChestTile::mayPlace(
	Level* level, int x, int y, int z, unsigned char face)
{
	const int offsetX[] = { -1, 1, 0, 0 };
	const int offsetZ[] = { 0, 0, -1, 1 };

	int chestCount = 0;

	for (int i = 0; i < 4; ++i) {
		int nx = x + offsetX[i];
		int nz = z + offsetZ[i];

		if (level->getTile(nx, y, nz) != id)
			continue;

		// A chest can have only one horizontal partner.
		if (++chestCount > 1)
			return false;

		// Do not extend an existing double chest into a triple
		// or an L-shaped chest arrangement.
		for (int j = 0; j < 4; ++j) {
			int ax = nx + offsetX[j];
			int az = nz + offsetZ[j];

			if (ax == x && az == z)
				continue;

			if (level->getTile(ax, y, az) == id)
				return false;
		}
	}

	return true;
}

void ChestTile::neighborChanged( Level* level, int x, int y, int z, int type )
{
	super::neighborChanged(level, x, y, z, type);
	recalcLockDir(level, x, y, z);
	TileEntity* te = level->getTileEntity(x, y, z);
	if (te != NULL) te->clearCache();
}

void ChestTile::onRemove( Level* level, int x, int y, int z )
{
	if (!level->isClientSide) {
		TileEntity* te = level->getTileEntity(x, y, z);
		if (te != NULL && te->type == TileEntityType::Chest) {
			ChestTileEntity* chest = (ChestTileEntity*)te;

			// Drop only this physical chest's 27 stored slots.
			for (int i = 0; i < 9 * 3; ++i) {
				ItemInstance* item =
					chest->FillingContainer::getItem(i);
				if (item != NULL) {
					float xo = random.nextFloat() * 0.8f + 0.1f;
					float yo = random.nextFloat() * 0.8f + 0.1f;
					float zo = random.nextFloat() * 0.8f + 0.1f;

					while (item->count > 0) {
						int count = random.nextInt(21) + 10;
						if (count > item->count) count = item->count;
						item->count -= count;
						ItemEntity* itemEntity = new ItemEntity(level, x + xo, y + yo, z + zo, ItemInstance(item->id, count, item->getAuxValue()));
						float pow = 0.05f;
						itemEntity->xd = (float) random.nextGaussian() * pow;
						itemEntity->yd = (float) random.nextGaussian() * pow + 0.2f;
						itemEntity->zd = (float) random.nextGaussian() * pow;
						//if (item->hasTag()) {
						//    itemEntity->item->setTag((CompoundTag*) item->getTag()->copy());
						//}
						level->addEntity(itemEntity);
					}
				}
			}
		}
	}
	super::onRemove(level, x, y, z);
}

bool ChestTile::use(
	Level* level, int x, int y, int z, Player* player)
{
	TileEntity* te = level->getTileEntity(x, y, z);

	if (!TileEntity::isType(te, TileEntityType::Chest))
		return true;

	ChestTileEntity* chest = (ChestTileEntity*)te;
	chest->checkNeighbors();

	// Either half being blocked prevents opening the pair.
	if (level->isSolidBlockingTile(x, y + 1, z))
		return true;

	if (chest->n && level->isSolidBlockingTile(
		chest->n->x, chest->n->y + 1, chest->n->z))
		return true;

	if (chest->s && level->isSolidBlockingTile(
		chest->s->x, chest->s->y + 1, chest->s->z))
		return true;

	if (chest->w && level->isSolidBlockingTile(
		chest->w->x, chest->w->y + 1, chest->w->z))
		return true;

	if (chest->e && level->isSolidBlockingTile(
		chest->e->x, chest->e->y + 1, chest->e->z))
		return true;

	if (level->isClientSide)
		return true;

	// Always open through the west or north half if paired.
	// This keeps inventory ordering and lid animation consistent.
	if (chest->w)
		chest = chest->w;
	else if (chest->n)
		chest = chest->n;

	player->openContainer(chest);
	return true;
}

TileEntity* ChestTile::newTileEntity()
{
	return TileEntityFactory::createTileEntity(TileEntityType::Chest);
}

bool ChestTile::isFullChest( Level* level, int x, int y, int z )
{
	return false; //@fullchest
	/*
	if (level->getTile(x, y, z) != id) return false;
	if (level->getTile(x - 1, y, z) == id) return true;
	if (level->getTile(x + 1, y, z) == id) return true;
	if (level->getTile(x, y, z - 1) == id) return true;
	if (level->getTile(x, y, z + 1) == id) return true;
	return false;
	*/
}
