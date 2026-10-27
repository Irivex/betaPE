#include "MobSpawner.h"
#include "LevelConstants.h"
#include "../Difficulty.h"

#include "Level.h"
#include "biome/Biome.h"
#include "material/Material.h"
#include "../entity/EntityTypes.h"
#include "../entity/MobFactory.h"
#include "../entity/MobCategory.h"
#include "../entity/player/Player.h"

//#include "../entity/animal/Sheep.h"
//#include "tile/BedTile.h"

std::map<ChunkPos, bool> MobSpawner::chunksToPoll;

static int _bedEnemies[] = {
    MobTypes::Spider,
    MobTypes::Zombie,
    MobTypes::Skeleton,
    MobTypes::PigZombie
};

static const std::vector<int> bedEnemies(
    _bedEnemies,
    _bedEnemies + sizeof(_bedEnemies) / sizeof(_bedEnemies[0])
);

/*static*/
int MobSpawner::tick(Level* level, bool spawnEnemies, bool spawnFriendlies) {

    if (!spawnEnemies && !spawnFriendlies) {
        return 0;
    }

    chunksToPoll.clear();

    // Calculate the actual world dimensions in chunks.
    const int chunkWidth =
        LevelConstants::LEVEL_WIDTH / LevelConstants::CHUNK_WIDTH;

    const int chunkDepth =
        LevelConstants::LEVEL_DEPTH / LevelConstants::CHUNK_DEPTH;

    // Only check chunks within 128 blocks of one player per spawning pass.
    // Friendly and hostile spawning use separate passes.
    if (spawnFriendlies) {
        spawnEnemies = false;

        static unsigned int friendlyPid = 0;

        if (!level->players.empty()) {
            if (friendlyPid >= level->players.size()) {
                friendlyPid = 0;
            }

            Player* p = level->players[friendlyPid];
            friendlyPid = (friendlyPid + 1) % level->players.size();

            const int playerChunkX = Mth::floor(p->x / 16);
            const int playerChunkZ = Mth::floor(p->z / 16);
            const int radius = 128 / 16;

            for (int x = -radius; x <= radius; ++x) {
                for (int z = -radius; z <= radius; ++z) {
                    const int cx = playerChunkX + x;
                    const int cz = playerChunkZ + z;

                    if (cx >= 0 && cx < chunkWidth &&
                        cz >= 0 && cz < chunkDepth) {
                        chunksToPoll.insert(
                            std::make_pair(ChunkPos(cx, cz), false)
                        );
                    }
                }
            }
        }
    }
    else {
        // Hostile mobs: check chunks within 128 blocks of one player.
        static unsigned int hostilePid = 0;

        if (!level->players.empty()) {
            if (hostilePid >= level->players.size()) {
                hostilePid = 0;
            }

            Player* p = level->players[hostilePid];
            hostilePid = (hostilePid + 1) % level->players.size();

            const int playerChunkX = Mth::floor(p->x / 16);
            const int playerChunkZ = Mth::floor(p->z / 16);
            const int radius = 128 / 16;

            for (int x = -radius; x <= radius; ++x) {
                for (int z = -radius; z <= radius; ++z) {
                    const int cx = playerChunkX + x;
                    const int cz = playerChunkZ + z;

                    if (cx >= 0 && cx < chunkWidth &&
                        cz >= 0 && cz < chunkDepth) {
                        chunksToPoll.insert(
                            std::make_pair(ChunkPos(cx, cz), false)
                        );
                    }
                }
            }
        }
    }

    int count = 0;
    Pos spawnPos = level->getSharedSpawnPos();

    for (int i = 0; i < MobCategory::numValues; ++i) {
        const MobCategory& mobCategory = *MobCategory::values[i];

        if ((mobCategory.isFriendly() && !spawnFriendlies) ||
            (!mobCategory.isFriendly() && !spawnEnemies)) {
            continue;
        }

        // Use the category's normal cap by default.
        // Only the hostile monster category uses difficulty-based caps.
        const bool isMonsterCategory =
            (&mobCategory == &MobCategory::monster);

        int maxInstances = mobCategory.getMaxInstancesPerLevel();

        if (isMonsterCategory) {
            switch (level->difficulty) {
            case Difficulty::PEACEFUL:
                maxInstances = 0;
                break;

            case Difficulty::EASY:
                maxInstances = 15;
                break;

            case Difficulty::NORMAL:
                maxInstances = 25;
                break;

            case Difficulty::HARD:
                maxInstances = 35;
                break;

            default:
                maxInstances = 25;
                break;
            }
        }

        int numMobs =
            level->countInstanceOfBaseType(mobCategory.getBaseClassId());

        // Don't attempt spawning if the category is already at its cap.
        if (numMobs >= maxInstances) {
            continue;
        }

        // Process candidate chunks around the selected player.
    chunkLoop:
        for (std::map<ChunkPos, bool>::iterator it = chunksToPoll.begin();
            it != chunksToPoll.end(); ++it) {

            // Stop this category as soon as its population cap is reached.
            if (numMobs >= maxInstances) {
                goto categoryDone;
            }

            const ChunkPos& cp = it->first;

            TilePos start =
                getRandomPosWithin(level, cp.x * 16, cp.z * 16);

            int xStart = start.x;
            int yStart = start.y;
            int zStart = start.z;

            if (level->isSolidBlockingTile(xStart, yStart, zStart)) {
                continue;
            }

            if (level->getMaterial(xStart, yStart, zStart) !=
                mobCategory.getSpawnPositionMaterial()) {
                continue;
            }

            int clusterSize = 0;

            for (int dd = 0; dd < 3; ++dd) {
                int x = xStart;
                int y = yStart;
                int z = zStart;
                int ss = 6;

                Biome::MobSpawnerData currentMobType;
                int maxCreatureCount = 999;
                int currentCreatureCount = 0;

                for (int ll = 0; ll < 4; ++ll) {
                    if (numMobs >= maxInstances) {
                        goto categoryDone;
                    }

                    if (currentCreatureCount >= maxCreatureCount) {
                        break;
                    }

                    x += level->random.nextInt(ss) -
                        level->random.nextInt(ss);

                    y += level->random.nextInt(1) -
                        level->random.nextInt(1);

                    z += level->random.nextInt(ss) -
                        level->random.nextInt(ss);

                    if (isSpawnPositionOk(mobCategory, level, x, y, z)) {
                        const float xx = static_cast<float>(x) + 0.5f;
                        const float yy = static_cast<float>(y);
                        const float zz = static_cast<float>(z) + 0.5f;

                        // Keep the existing minimum distance from players.
                        if (level->getNearestPlayer(
                            xx, yy, zz,
                            static_cast<float>(MIN_SPAWN_DISTANCE)) != NULL) {
                            continue;
                        }

                        // Keep the existing minimum distance from shared spawn.
                        const float xd = xx - spawnPos.x;
                        const float yd = yy - spawnPos.y;
                        const float zd = zz - spawnPos.z;

                        const float sd =
                            xd * xd + yd * yd + zd * zd;

                        if (sd <
                            MIN_SPAWN_DISTANCE * MIN_SPAWN_DISTANCE) {
                            continue;
                        }

                        if (!currentMobType.isValid()) {
                            currentMobType =
                                level->getRandomMobSpawnAt(
                                    mobCategory, x, y, z
                                );

                            if (!currentMobType.isValid()) {
                                break;
                            }

                            // Limit the population of individual hostile types.
                            if (isMonsterCategory) {
                                const int typeCount =
                                    level->countInstanceOfType(
                                        currentMobType.mobClassId
                                    );

                                const int typeMax =
                                    static_cast<int>(
                                        1.5f *
                                        currentMobType.randomWeight *
                                        maxInstances /
                                        Biome::defaultTotalEnemyWeight
                                        );

                                if (typeCount >= typeMax) {
                                    break;
                                }
                            }

                            maxCreatureCount =
                                currentMobType.minCount +
                                level->random.nextInt(
                                    1 + currentMobType.maxCount -
                                    currentMobType.minCount
                                );
                        }

                        Mob* tmp = MobFactory::getStaticTestMob(
                            currentMobType.mobClassId, level
                        );

                        if (!tmp) {
                            continue;
                        }

                        tmp->moveTo(xx, yy, zz, 0, 0);

                        if (!tmp->canSpawn()) {
                            continue;
                        }

                        Mob* mob = MobFactory::CreateMob(
                            currentMobType.mobClassId, level
                        );

                        if (!mob) {
                            continue;
                        }

                        if (addMob(
                            level,
                            mob,
                            xx,
                            yy,
                            zz,
                            level->random.nextFloat() * 360,
                            0,
                            false)) {

                            ++currentCreatureCount;
                            ++numMobs;
                            ++count;
                            ++clusterSize;

                            // Enforce the population cap immediately.
                            if (numMobs >= maxInstances) {
                                goto categoryDone;
                            }

                            if (clusterSize >=
                                mob->getMaxSpawnClusterSize()) {
                                goto chunkLoop;
                            }
                        }
                        else {
                            delete mob;
                        }
                    }
                }
            }
        }

    categoryDone:
        ;
    }

    return count;
}

/*static*/
void MobSpawner::postProcessSpawnMobs(
    Level* level,
    Biome* biome,
    int xo,
    int zo,
    int cellWidth,
    int cellHeight,
    Random* random) {

    Biome::MobList mobs = biome->getMobs(MobCategory::creature);

    if (mobs.empty()) {
        return;
    }

    while (random->nextFloat() < biome->getCreatureProbability()) {
        Biome::MobSpawnerData* type =
            (Biome::MobSpawnerData*)WeighedRandom::getRandomItem(
                &level->random, mobs
            );

        int count =
            type->minCount +
            random->nextInt(1 + type->maxCount - type->minCount);

        int x = xo + random->nextInt(cellWidth);
        int z = zo + random->nextInt(cellHeight);

        const int startX = x;
        const int startZ = z;

        for (int c = 0; c < count; ++c) {
            // Chunk-generation spawning must obey the passive creature cap too.
            if (level->countInstanceOfBaseType(
                MobCategory::creature.getBaseClassId()) >=
                MobCategory::creature.getMaxInstancesPerLevel()) {
                return;
            }

            bool success = false;

            for (int attempts = 0; !success && attempts < 4; ++attempts) {
                // These mobs always spawn at the topmost position.
                int y = level->getTopSolidBlock(x, z);

                if (isSpawnPositionOk(
                    MobCategory::creature, level, x, y, z)) {

                    const float xx = static_cast<float>(x) + 0.5f;
                    const float yy = static_cast<float>(y);
                    const float zz = static_cast<float>(z) + 0.5f;

                    // Recheck immediately before creating the mob.
                    if (level->countInstanceOfBaseType(
                        MobCategory::creature.getBaseClassId()) >=
                        MobCategory::creature.getMaxInstancesPerLevel()) {
                        return;
                    }

                    Mob* mob = MobFactory::CreateMob(
                        type->mobClassId, level
                    );

                    if (!mob) {
                        continue;
                    }

                    mob->moveTo(
                        xx, yy, zz,
                        random->nextFloat() * 360,
                        0
                    );

                    level->addEntity(mob);
                    finalizeMobSettings(mob, level, xx, yy, zz);

                    success = true;
                }

                x += random->nextInt(5) - random->nextInt(5);
                z += random->nextInt(5) - random->nextInt(5);

                while (x < xo || x >= (xo + cellWidth) ||
                    z < zo || z >= (zo + cellHeight)) {
                    x = startX + random->nextInt(5) - random->nextInt(5);
                    z = startZ + random->nextInt(5) - random->nextInt(5);
                }
            }
        }
    }
}

/*static*/
TilePos MobSpawner::getRandomPosWithin(Level* level, int xo, int zo) {
    int x = xo + level->random.nextInt(16);
    int y = level->random.nextInt(Level::DEPTH);
    int z = zo + level->random.nextInt(16);

    return TilePos(x, y, z);
}

/*static*/
bool MobSpawner::isSpawnPositionOk(
    const MobCategory& category,
    Level* level,
    int x,
    int y,
    int z) {

    if (category.getSpawnPositionMaterial() == Material::water) {
        return level->getMaterial(x, y, z)->isLiquid() &&
            !level->isSolidBlockingTile(x, y + 1, z);
    }
    else {
        return level->isSolidBlockingTile(x, y - 1, z) &&
            !level->isSolidBlockingTile(x, y, z) &&
            !level->getMaterial(x, y, z)->isLiquid() &&
            !level->isSolidBlockingTile(x, y + 1, z);
    }
}

/*static*/
void MobSpawner::finalizeMobSettings(
    Mob* mob,
    Level* level,
    float xx,
    float yy,
    float zz) {

    // @todo
    // if (mob instanceof Spider && level->random->nextInt(100) == 0) {
    //     Skeleton skeleton = new Skeleton(level);
    //     skeleton.moveTo(xx, yy, zz, mob.yRot, 0);
    //     level->addEntity(skeleton);
    //     skeleton.ride(mob);
    // } else if (mob instanceof Sheep) {
    //     ((Sheep) mob).setColor(Sheep.getSheepColor(level->random));
    // }

    if (mob->getEntityTypeId() == MobTypes::Sheep) {
        ((Sheep*)mob)->setColor(Sheep::getSheepColor(&level->random));
    }

    makeBabyMob(mob, 0.5f);
}

/*static*/
bool MobSpawner::addMob(
    Level* level,
    Mob* mob,
    float xx,
    float yy,
    float zz,
    float yRot,
    float xRot,
    bool force) {

    mob->moveTo(xx, yy, zz, yRot, xRot);

    if (force || mob->canSpawn()) {
        level->addEntity(mob);
        finalizeMobSettings(mob, level, xx, yy, zz);
        return true;
    }
    else {
        //LOGI("Couldn't add the entity\n");
        return false;
    }
}

void MobSpawner::makeBabyMob(Mob* mob, float probability) {
    static Random babyRandom(98495119L);

    if (MobTypes::BaseCreature == mob->getCreatureBaseType()) {
        if (babyRandom.nextFloat() < probability) {
            ((Animal*)mob)->setAge(
                -20 * 60 * SharedConstants::TicksPerSecond
            );
        }
    }
}