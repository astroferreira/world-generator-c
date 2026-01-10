#pragma once

#include "ISimulation.hpp"
#include <vector>

namespace worldgen {

struct ThermalParams {
    float talusAngle = 0.5f;
    float erosionRate = 0.5f;
    bool use8Neighbors = true;
    int maxTransferNeighbors = 2;  // Limit transfer to N steepest neighbors (reduces plateau formation)
};

class ThermalErosion : public ISimulation {
public:
    ThermalErosion();
    explicit ThermalErosion(const ThermalParams& params);

    std::string name() const override { return "Thermal Erosion"; }
    void initialize(TerrainData& terrain) override;
    void step() override;
    void reset() override;
    int iterations() const override { return m_iterations; }
    bool isComplete() const override { return m_changesLastStep == 0; }

    void setParams(const ThermalParams& params);

private:
    ThermalParams m_params;
    TerrainData* m_terrain = nullptr;
    int m_iterations = 0;
    int m_changesLastStep = 0;
    std::vector<float> m_deltaBuffer;

    // Thread-local delta buffers for lock-free parallel accumulation
    std::vector<std::vector<float>> m_threadLocalDeltas;
};

} // namespace worldgen
