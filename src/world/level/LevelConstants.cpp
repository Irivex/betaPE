#include "LevelConstants.h"

int LevelConstants::LEVEL_HEIGHT = 128;
int LevelConstants::CHUNK_CACHE_WIDTH = 16; // default 16 chunks = 256 blocks
int LevelConstants::CHUNK_WIDTH = 16; // in blocks
int LevelConstants::CHUNK_DEPTH = 16;
int LevelConstants::LEVEL_WIDTH = LevelConstants::CHUNK_CACHE_WIDTH * CHUNK_WIDTH;
int LevelConstants::LEVEL_DEPTH = LevelConstants::CHUNK_CACHE_WIDTH * CHUNK_DEPTH;
int LevelConstants::CHUNK_COLUMNS = LevelConstants::CHUNK_WIDTH * LevelConstants::CHUNK_DEPTH;
int LevelConstants::CHUNK_BLOCK_COUNT = LevelConstants::CHUNK_COLUMNS * LevelConstants::LEVEL_HEIGHT;

// Implement the setter function:
void LevelConstants::setWorldSize(int sizeInBlocks) {
    CHUNK_CACHE_WIDTH = sizeInBlocks / CHUNK_WIDTH;
    LEVEL_WIDTH = CHUNK_CACHE_WIDTH * CHUNK_WIDTH;
    LEVEL_DEPTH = CHUNK_CACHE_WIDTH * CHUNK_DEPTH;
    CHUNK_COLUMNS = CHUNK_WIDTH * CHUNK_DEPTH;
    CHUNK_BLOCK_COUNT = CHUNK_COLUMNS * LEVEL_HEIGHT;
}