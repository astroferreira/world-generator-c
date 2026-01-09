#include "simulation/SimulationManager.hpp"

namespace worldgen {

void SimulationManager::addSimulation(std::unique_ptr<ISimulation> simulation) {
    m_simulations.push_back(std::move(simulation));
}

void SimulationManager::clear() {
    m_simulations.clear();
    m_currentIndex = 0;
}

void SimulationManager::initialize(TerrainData& terrain) {
    m_terrain = &terrain;
    m_currentIndex = 0;
    for (auto& sim : m_simulations) {
        sim->initialize(terrain);
    }
}

void SimulationManager::step() {
    if (m_currentIndex >= m_simulations.size()) {
        // All sequential simulations complete - only step continuous ones
        for (auto& sim : m_simulations) {
            if (sim->isContinuous()) {
                sim->step();
            }
        }
        return;
    }

    auto& currentSim = m_simulations[m_currentIndex];

    // Step the current sequential simulation
    currentSim->step();

    if (m_callback) {
        m_callback(currentSim->name(), currentSim->iterations());
    }

    // Also step any continuous simulations (except if it's the current one)
    for (size_t i = 0; i < m_simulations.size(); ++i) {
        if (i != m_currentIndex && m_simulations[i]->isContinuous()) {
            m_simulations[i]->step();
        }
    }

    if (currentSim->isComplete()) {
        ++m_currentIndex;
    }
}

bool SimulationManager::isComplete() const {
    return m_currentIndex >= m_simulations.size();
}

void SimulationManager::setUpdateCallback(UpdateCallback callback) {
    m_callback = std::move(callback);
}

const ISimulation* SimulationManager::current() const {
    if (m_currentIndex < m_simulations.size()) {
        return m_simulations[m_currentIndex].get();
    }
    return nullptr;
}

} // namespace worldgen
