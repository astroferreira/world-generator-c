#pragma once

#include "ISimulation.hpp"
#include "utils/Random.hpp"
#include <vector>

namespace worldgen {

struct HydraulicParams {
    int dropletsPerStep = 50000;
    int maxDropletLifetime = 30;
    float inertia = 0.05f;
    float sedimentCapacity = 4.0f;
    float minSedimentCapacity = 0.01f;
    float depositSpeed = 0.3f;
    float erodeSpeed = 0.3f;
    float evaporateSpeed = 0.01f;
    float gravity = 4.0f;
    int erosionRadius = 3;
    float initialWater = 1.0f;
    float initialSpeed = 1.0f;
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

    std::vector<std::vector<int>> m_erosionBrushIndices;
    std::vector<std::vector<float>> m_erosionBrushWeights;

    void precomputeErosionBrush();
    void simulateDroplet();

    struct HeightAndGradient {
        float height;
        float gradientX;
        float gradientY;
    };
    HeightAndGradient calculateHeightAndGradient(float posX, float posY);
};

} // namespace worldgen
