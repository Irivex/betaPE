#ifndef NET_MINECRAFT_CLIENT_MODEL__ChestModel_H__
#define NET_MINECRAFT_CLIENT_MODEL__ChestModel_H__

#include "Model.h"
#include "geom/ModelPart.h"

class ChestModel : public Model
{
public:
    // false = normal chest, true = double chest.
    ChestModel(bool large = false)
        : lid(0, 0),
        lock(0, 0),
        bottom(0, 0)
    {
        lid.setModel(this);
        lock.setModel(this);
        bottom.setModel(this);

        // Large chest textures use a wider atlas.
        const int textureWidth = large ? 128 : 64;
        const int chestWidth = large ? 30 : 14;

        // Lid.
        lid.setTexSize(textureWidth, 64);
        lid.addBox(
            0.0f, -5, -14,
            chestWidth, 5, 14, 0
        );
        lid.x = 1;
        lid.y = 7;
        lid.z = 15;

        // Lock.
        lock.setTexSize(textureWidth, 64);
        lock.addBox(
            -1, -2, -15,
            2, 4, 1, 0
        );
        lock.x = large ? 16 : 8;
        lock.y = 7;
        lock.z = 15;

        // Chest body uses a different region of the texture atlas.
        bottom.setTexSize(textureWidth, 64);
        bottom.texOffs(0, 19);
        bottom.addBox(
            0.0f, 0, 0,
            chestWidth, 10, 14, 0
        );
        bottom.x = 1;
        bottom.y = 6;
        bottom.z = 1;
    }

    // Render lid, lock and body.
    void render()
    {
        lock.xRot = lid.xRot;

        lid.render(1 / 16.0f);
        lock.render(1 / 16.0f);
        bottom.render(1 / 16.0f);
    }

    ModelPart lid;
    ModelPart lock;
    ModelPart bottom;
};

#endif /* NET_MINECRAFT_CLIENT_MODEL__ChestModel_H__ */