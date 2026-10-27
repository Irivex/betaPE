#ifndef NET_MINECRAFT_WORLD_LEVEL_TILE__CakeTile_H__
#define NET_MINECRAFT_WORLD_LEVEL_TILE__CakeTile_H__

#include "Tile.h"

class CakeTile : public Tile
{
    typedef Tile super;

public:
    CakeTile(int id);

    bool use(Level* level, int x, int y, int z, Player* player);

    int getTexture(int face);
    int getTexture(int face, int data);
    int getRenderShape();

    bool isCubeShaped();
    bool isSolidRender();

    void updateShape(LevelSource* level, int x, int y, int z);
    void setShape();

    bool mayPlace(Level* level, int x, int y, int z);
    bool canSurvive(Level* level, int x, int y, int z);
    void neighborChanged(Level* level, int x, int y, int z, int type);

    // Cakes do not drop themselves when broken.
    int getResource(int data, Random* random);
};

#endif