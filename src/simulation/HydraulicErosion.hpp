#pragma once

#include "ISimulation.hpp"
#include "utils/Random.hpp"
#include <vector>

#ifdef WORLDGEN_USE_OPENMP
#include <omp.h>
#endif

namespace worldgen {

struct HydraulicParams {
    int dropletsPerStep = 100000;
    int maxDropletLifetime = 64;    // Safety cap - most terminate at sea/lake before this
    float inertia = 0.05f;
    float sedimentCapacity = 4.0f;
    float minSedimentCapacity = 0.01f;
    float depositSpeed = 0.3f;
    float erodeSpeed = 0.2f;        // Gentler erosion to avoid holes
    float evaporateSpeed = 0.02f;   // Moderate evaporation
    float gravity = 4.0f;
    int erosionRadius = 3;
    float initialWater = 1.0f;
    float initialSpeed = 1.0f;
    int parallelChunks = 4;  // Grid partitioning for parallel simulation

    // Deposition control - reduces ridge artifacts
    float depositConcentration = 0.7f;  // 0=full bilinear spread, 1=single cell

    // Termination conditions - droplets end at water bodies
    float seaLevel = 0.32f;         // Droplets stop when reaching sea level
    float minWaterForTermination = 0.01f;  // Minimum water depth to consider as lake
};

class HydraulicErosion : public ISimulation {
public:
    HydraulicErosion();
    explicit HydraulicErosion(const HydraulicParams& params);

    std::string name() const override { return "Hydraulic Erosion"; }
    void initialize(TerrainData& terrain) override;
    void step() override;
    void reset() override;
    int iterations() const override { return m_iterations; }

    void setParams(const HydraulicParams& params);

private:
    HydraulicParams m_params;
    TerrainData* m_terrain = nullptr;
    Random m_random;
    int m_iterations = 0;

    // Precomputed erosion brush (flattened for better cache locality)
    struct BrushData {
        std::vector<int> indices;
        std::vector<float> weights;
    };
    std::vector<BrushData> m_erosionBrush;

    // Cached terrain dimensions
    int m_width = 0;
    int m_height = 0;

    void precomputeErosionBrush();
    void simulateDroplet(Random& rng, int chunkX, int chunkY, int chunksPerSide);
    void simulateDropletBatch(int startIdx, int count, Random& rng);

    struct HeightAndGradient {
        float height;
        float gradientX;
        float gradientY;
    };

    // Inline height calculation for better performance
    HeightAndGradient calculateHeightAndGradient(float posX, float posY) const;

    // Get 4 corner heights for bilinear interpolation (cache-friendly)
    void getCornerHeights(int x, int y, float& h00, float& h10, float& h01, float& h11) const;
};

} // namespace worldgen
