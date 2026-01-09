#include "core/Application.hpp"
#include "io/ImageExporter.hpp"
#include <iostream>
#include <sstream>

namespace worldgen {

Application::Application() = default;

Application::Application(const AppConfig& config)
    : m_config(config)
{
}

Application::~Application() {
    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
    }
    SDL_Quit();
}

bool Application::initialize() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    try {
        m_window = std::make_unique<Window>("World Generator", m_config.windowWidth, m_config.windowHeight);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create window: " << e.what() << std::endl;
        return false;
    }

    m_renderer = SDL_CreateRenderer(
        m_window->get(),
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!m_renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        return false;
    }

    m_colorMapper.applyPreset(ColorMapper::Preset::Terrain);
    m_colorMapper.buildLUT(4096);

    return true;
}

void Application::run() {
    m_running = true;

    generateTerrain();

    const Uint32 targetFrameTime = 1000 / m_config.targetFPS;

    while (m_running) {
        Uint32 frameStart = SDL_GetTicks();

        processEvents();

        if (m_simulationRunning && !m_simManager.isComplete()) {
            for (int i = 0; i < m_config.simulationStepsPerFrame; ++i) {
                m_simManager.step();
            }
            m_needsRedraw = true;
            updateTitle();
        }

        if (m_needsRedraw) {
            render();
            m_needsRedraw = false;
        }

        Uint32 frameTime = SDL_GetTicks() - frameStart;
        if (frameTime < targetFrameTime) {
            SDL_Delay(targetFrameTime - frameTime);
        }
    }
}

void Application::generateTerrain() {
    std::cout << "Generating terrain (seed: " << m_seed << ")..." << std::endl;

    TerrainGenConfig config;
    config.width = m_config.terrainWidth;
    config.height = m_config.terrainHeight;
    config.seed = m_seed;

    m_generator.configure(config);
    m_terrain = m_generator.generate([](float progress, const std::string& stage) {
        std::cout << "  " << stage << " (" << static_cast<int>(progress * 100) << "%)" << std::endl;
    });

    // Create or recreate heightmap view
    m_heightmapView = std::make_unique<HeightmapView>(
        m_renderer,
        m_terrain.width(),
        m_terrain.heightDim()
    );

    // Setup simulations
    m_simManager.clear();
    m_simManager.addSimulation(std::make_unique<ThermalErosion>());
    m_simManager.addSimulation(std::make_unique<HydraulicErosion>());
    m_simManager.initialize(m_terrain);

    m_needsRedraw = true;
    updateTitle();
    std::cout << "Generation complete!" << std::endl;
}

void Application::startSimulations() {
    m_simulationRunning = true;
    std::cout << "Simulations started" << std::endl;
}

void Application::pauseSimulations() {
    m_simulationRunning = false;
    std::cout << "Simulations paused" << std::endl;
}

void Application::stepSimulation() {
    if (!m_simManager.isComplete()) {
        m_simManager.step();
        m_needsRedraw = true;
        updateTitle();
    }
}

void Application::exportHeightmap(const std::string& filename) {
    if (ImageExporter::exportPNG(*m_terrain.height, m_colorMapper, filename)) {
        std::cout << "Exported to " << filename << std::endl;
    } else {
        std::cerr << "Failed to export to " << filename << std::endl;
    }
}

void Application::processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                m_running = false;
                break;
            case SDL_KEYDOWN:
                handleKeyDown(event.key.keysym.sym);
                break;
        }
    }
}

void Application::handleKeyDown(SDL_Keycode key) {
    switch (key) {
        case SDLK_ESCAPE:
        case SDLK_q:
            m_running = false;
            break;
        case SDLK_SPACE:
            if (m_simulationRunning) {
                pauseSimulations();
            } else {
                startSimulations();
            }
            break;
        case SDLK_r:
            m_seed = static_cast<int>(SDL_GetTicks());
            generateTerrain();
            break;
        case SDLK_s:
            stepSimulation();
            break;
        case SDLK_e:
            exportHeightmap("terrain.png");
            break;
        case SDLK_g:
            // Toggle grayscale
            static bool grayscale = false;
            grayscale = !grayscale;
            m_colorMapper.applyPreset(grayscale ? ColorMapper::Preset::Grayscale : ColorMapper::Preset::Terrain);
            m_needsRedraw = true;
            break;
    }
}

void Application::render() {
    m_heightmapView->update(*m_terrain.height, m_colorMapper);

    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);

    SDL_Rect destRect = {0, 0, m_config.windowWidth, m_config.windowHeight};
    m_heightmapView->render(destRect);

    SDL_RenderPresent(m_renderer);
}

void Application::updateTitle() {
    std::ostringstream title;
    title << "World Generator";

    if (auto* sim = m_simManager.current()) {
        title << " - " << sim->name() << " (iter: " << sim->iterations() << ")";
    } else {
        title << " - Complete";
    }

    if (m_simulationRunning) {
        title << " [RUNNING]";
    } else {
        title << " [PAUSED]";
    }

    m_window->setTitle(title.str());
}

} // namespace worldgen
