#pragma once

#include "renderer/Window.hpp"
#include "renderer/HeightmapView.hpp"
#include "renderer/ColorMapper.hpp"
#include "renderer/ObliqueRenderer.hpp"
#include "terrain/TerrainData.hpp"
#include "generation/TerrainGenerator.hpp"
#include "simulation/SimulationManager.hpp"
#include "simulation/HydraulicErosion.hpp"
#include "simulation/ThermalErosion.hpp"
#include "simulation/HydrologySimulation.hpp"
#include "simulation/GlacierSystem.hpp"
#include <memory>
#include <string>

namespace worldgen {

enum class ViewMode {
    TopDown,    // Classic 2D top-down view
    Oblique     // 2.5D oblique view with shading
};

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
    std::unique_ptr<ObliqueRenderer> m_obliqueRenderer;
    ColorMapper m_colorMapper;

    bool m_running = false;
    bool m_simulationRunning = false;
    bool m_needsRedraw = true;
    bool m_waterRenderingEnabled = true;
    int m_seed = 42;
    ViewMode m_viewMode = ViewMode::TopDown;

    // Auto-export settings for unattended simulation monitoring
    bool m_autoExportEnabled = true;
    int m_autoExportInterval = 50;    // Export every N simulation steps
    int m_lastExportStep = 0;
    int m_totalSimulationSteps = 0;

    // Zoom and pan state
    float m_zoomLevel = 1.0f;
    float m_viewOffsetX = 0.0f;
    float m_viewOffsetY = 0.0f;
    static constexpr float MIN_ZOOM = 0.25f;
    static constexpr float MAX_ZOOM = 8.0f;
    static constexpr float ZOOM_STEP = 1.25f;
    static constexpr float PAN_STEP = 50.0f;

    void processEvents();
    void handleKeyDown(SDL_Keycode key);
    void render();
    void updateTitle();
    void printHelp();
    void autoExport();
};

} // namespace worldgen
