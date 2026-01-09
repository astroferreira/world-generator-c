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
    if (m_currentIndex >= m_simulations.size()) return;

    auto& sim = m_simulations[m_currentIndex];
    sim->step();

    if (m_callback) {
        m_callback(sim->name(), sim->iterations());
    }

    if (sim->isComplete()) {
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
