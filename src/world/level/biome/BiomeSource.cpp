#include "BiomeSource.h"
#include "Biome.h"
#include "../Level.h"
#include "../ChunkPos.h"
#include <climits>

const float BiomeSource::zoom = 2 * 1;

const float BiomeSource::tempScale = zoom / 80.0f;
const float BiomeSource::downfallScale = zoom / 40.0f;
const float BiomeSource::noiseScale = 1 / 4.0f;

BiomeSource::BiomeSource()
:	temperatureMap(NULL),
	downfallMap(NULL),
	noiseMap(NULL),
	lenTemperatures(0),
	lenDownfalls(0),
	lenNoises(0),
	lenBiomes(0),
	temperatures(NULL),
	downfalls(NULL),
	noises(NULL),
	biomes(NULL)
{
	biomes = new Biome*[16*16];
}

void BiomeSource::ensureBufferCapacity(int size)
{
	if (size <= 0)
		return;

	if (lenTemperatures < size)
	{
		float* newBuffer = new float[size];
		delete[] temperatures;
		temperatures = newBuffer;
		lenTemperatures = size;
	}

	if (lenDownfalls < size)
	{
		float* newBuffer = new float[size];
		delete[] downfalls;
		downfalls = newBuffer;
		lenDownfalls = size;
	}

	if (lenNoises < size)
	{
		float* newBuffer = new float[size];
		delete[] noises;
		noises = newBuffer;
		lenNoises = size;
	}

	if (lenBiomes < size)
	{
		Biome** newBuffer = new Biome * [size];
		delete[] biomes;
		biomes = newBuffer;
		lenBiomes = size;
	}
}

BiomeSource::BiomeSource( Level* level )
:	rndTemperature(level->getSeed() * 9871),
	rndDownfall(level->getSeed() * 39811),
	rndNoise(level->getSeed() * 543321),
	lenTemperatures(0),
	lenDownfalls(0),
	lenNoises(0),
	lenBiomes(0),
	temperatures(NULL),
	downfalls(NULL),
	noises(NULL),
	biomes(NULL)

{
	generationOffsetX = level->getLevelData()->getGenerationOffsetX();
	generationOffsetZ = level->getLevelData()->getGenerationOffsetZ();
	temperatureMap = new PerlinSimplexNoise(&rndTemperature, 4);
	downfallMap = new PerlinSimplexNoise(&rndDownfall, 4);
	noiseMap = new PerlinSimplexNoise(&rndNoise, 2);

	biomes = new Biome*[16*16];
	temperatures = new float[16*16];
}

BiomeSource::~BiomeSource() {
	LOGI("Deleting biome maps...\n");
	delete temperatureMap;
	delete downfallMap;
	delete noiseMap;

	LOGI("Deleting biome data arrays...\n");
	delete[] temperatures;
	delete[] downfalls;
	delete[] noises;
	delete[] biomes;
}

Biome* BiomeSource::getBiome( const ChunkPos& chunk )
{
	return getBiome(chunk.x << 4, chunk.z << 4);
}

Biome* BiomeSource::getBiome( int x, int z )
{
	return getBiomeBlock(x, z, 1, 1)[0];
}

float BiomeSource::getTemperature( int x, int z )
{
	x -= generationOffsetX;
	z -= generationOffsetZ;
	temperatures = temperatureMap->getRegion(temperatures, x, z, 1, 1, tempScale, tempScale, 0.5f);
	return temperatures[0];
}

Biome** BiomeSource::getBiomeBlock( int x, int z, int w, int h )
{
    biomes = getBiomeBlock(biomes, x, z, w, h);
    return biomes;
}

Biome** BiomeSource::getBiomeBlock(
	Biome** biomes__, int x, int z, int w, int h)
{
	if (w <= 0 || h <= 0 || w > INT_MAX / h)
		return biomes;

	x -= generationOffsetX;
	z -= generationOffsetZ;

	ensureBufferCapacity(w * h);

	temperatures = temperatureMap->getRegion(
		temperatures, x, z, w, h,
		tempScale, tempScale, 0.25f);

	downfalls = downfallMap->getRegion(
		downfalls, x, z, w, h,
		downfallScale, downfallScale, 0.3333f);

	noises = noiseMap->getRegion(
		noises, x, z, w, h,
		noiseScale, noiseScale, 0.588f);

	int pp = 0;

	for (int yy = 0; yy < w; ++yy)
	{
		for (int xx = 0; xx < h; ++xx)
		{
			float noise = noises[pp] * 1.1f + 0.5f;

			float split2 = 0.01f;
			float split1 = 1 - split2;

			float temperature =
				(temperatures[pp] * 0.15f + 0.7f)
				* split1 + noise * split2;

			split2 = 0.002f;
			split1 = 1 - split2;

			float downfall =
				(downfalls[pp] * 0.15f + 0.5f)
				* split1 + noise * split2;

			temperature =
				1 - ((1 - temperature) * (1 - temperature));

			if (temperature < 0) temperature = 0;
			if (downfall < 0) downfall = 0;
			if (temperature > 1) temperature = 1;
			if (downfall > 1) downfall = 1;

			temperatures[pp] = temperature;
			downfalls[pp] = downfall;
			biomes[pp++] = Biome::getBiome(temperature, downfall);
		}
	}

	return biomes;
}

float* BiomeSource::getTemperatureBlock(
	float* temperatures__, int x, int z, int w, int h)
{
	if (w <= 0 || h <= 0 || w > INT_MAX / h)
		return temperatures;

	x -= generationOffsetX;
	z -= generationOffsetZ;

	ensureBufferCapacity(w * h);

	temperatures = temperatureMap->getRegion(
		temperatures, x, z, w, h,
		tempScale, tempScale, 0.25f);

	noises = noiseMap->getRegion(
		noises, x, z, w, h,
		noiseScale, noiseScale, 0.588f);

	int pp = 0;

	for (int yy = 0; yy < w; ++yy)
	{
		for (int xx = 0; xx < h; ++xx)
		{
			float noise = noises[pp] * 1.1f + 0.5f;

			float split2 = 0.01f;
			float split1 = 1 - split2;

			float temperature =
				(temperatures[pp] * 0.15f + 0.7f)
				* split1 + noise * split2;

			temperature =
				1 - ((1 - temperature) * (1 - temperature));

			if (temperature < 0) temperature = 0;
			if (temperature > 1) temperature = 1;

			temperatures[pp] = temperature;
			++pp;
		}
	}

	return temperatures;
}

float* BiomeSource::getDownfallBlock(
	float* downfalls__, int x, int z, int w, int h)
{
	if (w <= 0 || h <= 0 || w > INT_MAX / h)
		return downfalls;

	x -= generationOffsetX;
	z -= generationOffsetZ;

	ensureBufferCapacity(w * h);

	downfalls = downfallMap->getRegion(
		downfalls, x, z, w, h,
		downfallScale, downfallScale, 0.5f);

	return downfalls;
}
