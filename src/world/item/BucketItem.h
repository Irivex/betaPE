// add buckets

#ifndef NET_MINECRAFT_WORLD_ITEM__BucketItem_H__
#define NET_MINECRAFT_WORLD_ITEM__BucketItem_H__

#include "Item.h"
#include "ItemInstance.h"
#include "../entity/player/Player.h"
#include "../level/Level.h"
#include "../level/tile/Tile.h"

class BucketItem : public Item
{
    typedef Item super;

public:
    // contentsTileId:
    // 0                         = empty bucket
    // Tile::calmWater->id       = water bucket
    // Tile::calmLava->id        = lava bucket
    BucketItem(int id, int contentsTileId)
        : super(id),
        _contents(contentsTileId)
    {
        setMaxStackSize(1);
    }

    bool useOn(
        ItemInstance* instance,
        Player* player,
        Level* level,
        int x,
        int y,
        int z,
        int face,
        float,
        float,
        float)
    {
        if (!instance || !player || !level)
            return false;

        // ----------------------------------------------------
        // Empty bucket: pick up a liquid source.
        // ----------------------------------------------------
        if (_contents == 0)
        {
            int targetX = x;
            int targetY = y;
            int targetZ = z;

            int tileId = level->getTile(x, y, z);

            bool isWater = false;
            bool isLava = false;

            // If the raycast hit a liquid directly, it must be a
            // source block. Do not fall back to a neighbouring
            // source when the directly-hit liquid is flowing.
            if (isLiquidTile(tileId))
            {
                if (!isSourceLiquid(
                    level, x, y, z, isWater, isLava))
                {
                    return false;
                }
            }
            else
            {
                // The raycast may have hit a solid block next to
                // the liquid. Check the block on the clicked face.
                switch (face)
                {
                case 0: --targetY; break;
                case 1: ++targetY; break;
                case 2: --targetZ; break;
                case 3: ++targetZ; break;
                case 4: --targetX; break;
                case 5: ++targetX; break;
                default: return false;
                }

                if (!isSourceLiquid(
                    level,
                    targetX, targetY, targetZ,
                    isWater, isLava))
                {
                    return false;
                }
            }

            // Only transform the held item if the source block
            // was successfully removed.
            if (!level->setTileAndData(
                targetX, targetY, targetZ, 0, 0))
            {
                return false;
            }

            Item* filledBucket =
                isWater ? Item::bucket_water : Item::bucket_lava;

            if (!filledBucket)
                return false;

            instance->init(filledBucket->id, 1, 0);
            return true;
        }

        // ----------------------------------------------------
        // Filled bucket: place its liquid against the hit face.
        // ----------------------------------------------------
        int placeX = x;
        int placeY = y;
        int placeZ = z;

        switch (face)
        {
        case 0: --placeY; break;
        case 1: ++placeY; break;
        case 2: --placeZ; break;
        case 3: ++placeZ; break;
        case 4: --placeX; break;
        case 5: ++placeX; break;
        default: return false;
        }

        // Do not place outside the world's vertical bounds.
        if (placeY < 0 || placeY >= Level::DEPTH)
            return false;

        int existingTile = level->getTile(placeX, placeY, placeZ);

        // Allow placement into air or an existing liquid, but
        // never replace a solid block.
        if (existingTile != 0 && !isLiquidTile(existingTile))
            return false;

        // _contents is the calm/static fluid ID:
        // water 9 -> dynamic water 8
        // lava 11 -> dynamic lava 10
        const int dynamicTileId = _contents - 1;

        if (dynamicTileId <= 0 ||
            Tile::tiles[dynamicTileId] == NULL)
        {
            return false;
        }

        // Set the dynamic fluid as a source and schedule its first
        // tick so normal flow begins.
        if (!level->setTileAndData(
            placeX, placeY, placeZ, dynamicTileId, 0))
        {
            return false;
        }

        Tile* placedFluid = Tile::tiles[dynamicTileId];
        level->addToTickNextTick(
            placeX,
            placeY,
            placeZ,
            dynamicTileId,
            placedFluid->getTickDelay());

        // A successful pour returns the empty bucket.
        if (Item::bucket_empty)
            instance->init(Item::bucket_empty->id, 1, 0);

        return true;
    }

private:
    static bool isLiquidTile(int tileId)
    {
        return tileId == Tile::water->id
            || tileId == Tile::calmWater->id
            || tileId == Tile::lava->id
            || tileId == Tile::calmLava->id;
    }

    static bool isSourceLiquid(
        Level* level,
        int x,
        int y,
        int z,
        bool& isWater,
        bool& isLava)
    {
        const int tileId = level->getTile(x, y, z);
        const int data = level->getData(x, y, z);

        isWater = tileId == Tile::water->id
            || tileId == Tile::calmWater->id;

        isLava = tileId == Tile::lava->id
            || tileId == Tile::calmLava->id;

        // In this engine, level 0 represents a source. Values
        // above 0 represent flowing liquid, including falling fluid.
        return (isWater || isLava) && data == 0;
    }

    int _contents;
};

#endif
