#pragma once

#include "ISimulation.hpp"
#include <vector>
#include <memory>
#include <functional>

namespace worldgen {

class SimulationManager {
public:
    using UpdateCallback = std::function<void(const std::string& simName, int iteration)>;

    SimulationManager() = default;

    void addSimulation(std::unique_ptr<ISimulation> simulation);
    void clear();
    void initialize(TerrainData& terrain);
    void step();
    bool isComplete() const;
    void setUpdateCallback(UpdateCallback callback);
    const ISimulation* current() const;

private:
    std::vector<std::unique_ptr<ISimulation>> m_simulations;
    size_t m_currentIndex = 0;
    UpdateCallback m_callback;
    TerrainData* m_terrain = nullptr;
};

} // namespace worldgen
