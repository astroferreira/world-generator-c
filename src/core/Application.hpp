#pragma once

#include "renderer/Window.hpp"
#include "renderer/HeightmapView.hpp"
#include "renderer/ColorMapper.hpp"
#include "terrain/TerrainData.hpp"
#include "generation/TerrainGenerator.hpp"
#include "simulation/SimulationManager.hpp"
#include "simulation/HydraulicErosion.hpp"
#include "simulation/ThermalErosion.hpp"
#include <memory>
#include <string>

namespace worldgen {

struct AppConfig {
    int windowWidth = 1024;
    int windowHeight = 1024;
    int terrainWidth = 2048;
    int terrainHeight = 2048;
    int targetFPS = 60;
    int simulationStepsPerFrame = 1;
};

class Application {
public:
    Application();
    explicit Application(const AppConfig& config);
    ~Application();

    bool initialize();
    void run();
    void generateTerrain();
    void startSimulations();
    void pauseSimulations();
    void stepSimulation();
    void exportHeightmap(const std::string& filename);

private:
    AppConfig m_config;

    std::unique_ptr<Window> m_window;
    SDL_Renderer* m_renderer = nullptr;

    TerrainData m_terrain;
    TerrainGenerator m_generator;
    SimulationManager m_simManager;
    std::unique_ptr<HeightmapView> m_heightmapView;
    ColorMapper m_colorMapper;

    bool m_running = false;
    bool m_simulationRunning = false;
    bool m_needsRedraw = true;
    int m_seed = 42;

    void processEvents();
    void handleKeyDown(SDL_Keycode key);
    void render();
    void updateTitle();
};

} // namespace worldgen
