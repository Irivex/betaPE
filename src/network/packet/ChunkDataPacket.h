#ifndef NET_MINECRAFT_NETWORK_PACKET__ChunkDataPacket_H__
#define NET_MINECRAFT_NETWORK_PACKET__ChunkDataPacket_H__

#include "../Packet.h"
#include "../../world/level/chunk/LevelChunk.h"
#include "../../world/level/tile/Tile.h"

class ChunkDataPacket : public Packet
{
public:

	int x, z;
	RakNet::BitStream chunkData;
	LevelChunk* chunk;

	ChunkDataPacket()
	{
	}

	ChunkDataPacket(int x, int z, LevelChunk* chunk)
	:	x(x),
		z(z),
		chunk(chunk)
	{
	}

    void write(RakNet::BitStream* bitStream)
    {
        bitStream->Write(
            (RakNet::MessageID)(ID_USER_PACKET_ENUM + PACKET_CHUNKDATA));

        bitStream->Write(x);
        bitStream->Write(z);

        unsigned char* blockIds = chunk->getBlockData();
        DataLayer& blockData = chunk->data;

        const int setSize = LevelConstants::LEVEL_HEIGHT / 8;
        const int setShift = 4;

        // Find vertical sections containing water or lava.
        // These sections must be sent even if updateMap doesn't
        // contain flags for them, since liquid state may have changed.
        unsigned char liquidSectionBits = 0;

        for (int set = 0; set < 8; ++set)
        {
            const int yOffset = set << setShift;
            bool foundLiquid = false;

            for (int i = 0;
                i < LevelConstants::CHUNK_COLUMNS && !foundLiquid;
                ++i)
            {
                const int colX = i % LevelConstants::CHUNK_WIDTH;
                const int colZ = i / LevelConstants::CHUNK_WIDTH;

                const int colDataPosition =
                    (colX << 11) | (colZ << 7);

                for (int localY = 0; localY < setSize; ++localY)
                {
                    const int tileId =
                        blockIds[colDataPosition + yOffset + localY] & 0xff;

                    if (tileId == Tile::water->id ||
                        tileId == Tile::calmWater->id ||
                        tileId == Tile::lava->id ||
                        tileId == Tile::calmLava->id)
                    {
                        liquidSectionBits |=
                            static_cast<unsigned char>(1 << set);

                        foundLiquid = true;
                        break;
                    }
                }
            }
        }

        chunkData.Reset();

        for (int i = 0; i < LevelConstants::CHUNK_COLUMNS; ++i)
        {
            // Preserve normal updates, and also synchronize entire
            // vertical sections that contain liquids.
            const unsigned char updateBits =
                static_cast<unsigned char>(
                    chunk->updateMap[i] | liquidSectionBits);

            chunkData.Write(updateBits);

            if (updateBits > 0)
            {
                const int colDataPosition =
                    ((i % LevelConstants::CHUNK_WIDTH) << 11) |
                    ((i / LevelConstants::CHUNK_WIDTH) << 7);

                for (int set = 0; set < 8; ++set)
                {
                    if ((updateBits & (1 << set)) != 0)
                    {
                        chunkData.Write(
                            (const char*)(
                                &blockIds[colDataPosition + (set << setShift)]),
                            setSize);

                        // Block metadata uses four bits per block.
                        chunkData.Write(
                            (const char*)(
                                &blockData.data[
                                    (colDataPosition + (set << setShift)) >> 1]),
                            setSize >> 1);
                    }
                }
            }
        }

        bitStream->Write(chunkData);
    }

	void read(RakNet::BitStream* bitStream)
	{
		bitStream->Read(x);
		bitStream->Read(z);
		bitStream->Read(chunkData);
	}

	void handle(const RakNet::RakNetGUID& source, NetEventCallback* callback)
	{
		callback->handle(source, (ChunkDataPacket*)this);
	}
};

#endif /*NET_MINECRAFT_NETWORK_PACKET__ChunkDataPacket_H__*/
