#ifndef NET_MINECRAFT_CLIENT_RENDERER_TILEENTITY__ChestRenderer_H__
#define NET_MINECRAFT_CLIENT_RENDERER_TILEENTITY__ChestRenderer_H__

//package net.minecraft.client.renderer.tileentity;

#include "TileEntityRenderer.h"
#include "../../model/ChestModel.h"

class ChestTileEntity;

class ChestRenderer : public TileEntityRenderer
{
public:
    ChestRenderer()
        : chestModel(false),
        largeChestModel(true)
    {
    }

    void render(
        TileEntity* chest, float x, float y, float z, float a);

private:
    ChestModel chestModel;
    ChestModel largeChestModel;
};

#endif /*NET_MINECRAFT_CLIENT_RENDERER_TILEENTITY__ChestRenderer_H__*/
