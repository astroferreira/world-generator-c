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

    printHelp();

    return true;
}

void Application::printHelp() {
    std::cout << "\n";
    std::cout << "=== World Generator Controls ===" << std::endl;
    std::cout << "\n";
    std::cout << "  SIMULATION:" << std::endl;
    std::cout << "    Space     - Start/Pause simulation" << std::endl;
    std::cout << "    S         - Step simulation once" << std::endl;
    std::cout << "    R         - Regenerate terrain (new seed)" << std::endl;
    std::cout << "\n";
    std::cout << "  VIEW:" << std::endl;
    std::cout << "    V         - Toggle 2D / 3D oblique view" << std::endl;
    std::cout << "    G         - Toggle grayscale / color" << std::endl;
    std::cout << "    W         - Toggle water rendering" << std::endl;
    std::cout << "    L         - Rotate light direction (3D view)" << std::endl;
    std::cout << "\n";
    std::cout << "  NAVIGATION:" << std::endl;
    std::cout << "    +/-       - Zoom in/out" << std::endl;
    std::cout << "    Arrows    - Pan view" << std::endl;
    std::cout << "    0         - Reset zoom and pan" << std::endl;
    std::cout << "\n";
    std::cout << "  OTHER:" << std::endl;
    std::cout << "    E         - Export terrain.png" << std::endl;
    std::cout << "    Q/Esc     - Quit" << std::endl;
    std::cout << "\n";
    std::cout << "=================================" << std::endl;
    std::cout << "\n";
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
                m_totalSimulationSteps++;
            }
            m_needsRedraw = true;
            updateTitle();

            // Auto-export at regular intervals
            if (m_autoExportEnabled) {
                autoExport();
            }
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

    // Reset auto-export state
    m_totalSimulationSteps = 0;
    m_lastExportStep = 0;

    TerrainGenConfig config;
    config.width = m_config.terrainWidth;
    config.height = m_config.terrainHeight;
    config.seed = m_seed;

    m_generator.configure(config);
    m_terrain = m_generator.generate([](float progress, const std::string& stage) {
        std::cout << "  " << stage << " (" << static_cast<int>(progress * 100) << "%)" << std::endl;
    });

    // Create or recreate heightmap view and oblique renderer
    m_heightmapView = std::make_unique<HeightmapView>(
        m_renderer,
        m_terrain.width(),
        m_terrain.heightDim()
    );

    m_obliqueRenderer = std::make_unique<ObliqueRenderer>(
        m_renderer,
        m_terrain.width(),
        m_terrain.heightDim()
    );

    // Setup simulations
    // Order: thermal -> hydrology (continuous) -> glacier -> hydraulic
    // Hydrology is continuous - it runs alongside all other simulations and
    // periodically recalculates rivers as terrain changes from erosion
    m_simManager.clear();
    m_simManager.addSimulation(std::make_unique<ThermalErosion>());
    m_simManager.addSimulation(std::make_unique<HydrologySimulation>());  // Continuous
    m_simManager.addSimulation(std::make_unique<GlacierSystem>());
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
    bool success;
    if (m_waterRenderingEnabled) {
        success = ImageExporter::exportWithWater(m_terrain, m_colorMapper, filename);
    } else {
        success = ImageExporter::exportPNG(*m_terrain.height, m_colorMapper, filename);
    }

    if (success) {
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
            m_zoomLevel = 1.0f;
            m_viewOffsetX = 0.0f;
            m_viewOffsetY = 0.0f;
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
        case SDLK_w:
            // Toggle water rendering
            m_waterRenderingEnabled = !m_waterRenderingEnabled;
            std::cout << "Water rendering: " << (m_waterRenderingEnabled ? "ON" : "OFF") << std::endl;
            m_needsRedraw = true;
            break;

        case SDLK_v:
            // Toggle view mode (top-down / oblique)
            m_viewMode = (m_viewMode == ViewMode::TopDown) ? ViewMode::Oblique : ViewMode::TopDown;
            std::cout << "View mode: " << (m_viewMode == ViewMode::TopDown ? "Top-Down" : "Oblique 2.5D") << std::endl;
            m_needsRedraw = true;
            break;

        case SDLK_l:
            // Rotate light direction
            if (m_obliqueRenderer) {
                m_obliqueRenderer->rotateLight(45.0f);
                std::cout << "Light rotated" << std::endl;
                m_needsRedraw = true;
            }
            break;

        // Zoom controls
        case SDLK_EQUALS:
        case SDLK_PLUS:
        case SDLK_KP_PLUS:
            if (m_zoomLevel < MAX_ZOOM) {
                m_zoomLevel *= ZOOM_STEP;
                if (m_zoomLevel > MAX_ZOOM) m_zoomLevel = MAX_ZOOM;
                std::cout << "Zoom: " << static_cast<int>(m_zoomLevel * 100) << "%" << std::endl;
                m_needsRedraw = true;
            }
            break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            if (m_zoomLevel > MIN_ZOOM) {
                m_zoomLevel /= ZOOM_STEP;
                if (m_zoomLevel < MIN_ZOOM) m_zoomLevel = MIN_ZOOM;
                std::cout << "Zoom: " << static_cast<int>(m_zoomLevel * 100) << "%" << std::endl;
                m_needsRedraw = true;
            }
            break;
        case SDLK_0:
            // Reset zoom and position
            m_zoomLevel = 1.0f;
            m_viewOffsetX = 0.0f;
            m_viewOffsetY = 0.0f;
            std::cout << "View reset" << std::endl;
            m_needsRedraw = true;
            break;

        // Pan controls
        case SDLK_LEFT:
            m_viewOffsetX += PAN_STEP;
            m_needsRedraw = true;
            break;
        case SDLK_RIGHT:
            m_viewOffsetX -= PAN_STEP;
            m_needsRedraw = true;
            break;
        case SDLK_UP:
            m_viewOffsetY += PAN_STEP;
            m_needsRedraw = true;
            break;
        case SDLK_DOWN:
            m_viewOffsetY -= PAN_STEP;
            m_needsRedraw = true;
            break;
    }
}

void Application::render() {
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);

    // Apply zoom and pan to destination rectangle
    int zoomedWidth = static_cast<int>(m_config.windowWidth * m_zoomLevel);
    int zoomedHeight = static_cast<int>(m_config.windowHeight * m_zoomLevel);

    // Center the zoomed view and apply pan offset
    int centerOffsetX = (m_config.windowWidth - zoomedWidth) / 2;
    int centerOffsetY = (m_config.windowHeight - zoomedHeight) / 2;

    SDL_Rect destRect = {
        centerOffsetX + static_cast<int>(m_viewOffsetX),
        centerOffsetY + static_cast<int>(m_viewOffsetY),
        zoomedWidth,
        zoomedHeight
    };

    if (m_viewMode == ViewMode::Oblique && m_obliqueRenderer) {
        // Oblique 2.5D view with shading
        m_obliqueRenderer->renderToTexture(m_terrain, m_colorMapper);
        m_obliqueRenderer->display(destRect);
    } else {
        // Top-down 2D view
        if (m_waterRenderingEnabled) {
            m_heightmapView->updateWithWater(m_terrain, m_colorMapper);
        } else {
            m_heightmapView->update(*m_terrain.height, m_colorMapper);
        }
        m_heightmapView->render(destRect);
    }

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

    if (m_zoomLevel != 1.0f) {
        title << " [" << static_cast<int>(m_zoomLevel * 100) << "%]";
    }

    if (m_viewMode == ViewMode::Oblique) {
        title << " [3D]";
    }

    m_window->setTitle(title.str());
}

void Application::autoExport() {
    // Export 3D view at regular intervals during simulation
    int stepsSinceLastExport = m_totalSimulationSteps - m_lastExportStep;

    if (stepsSinceLastExport >= m_autoExportInterval) {
        m_lastExportStep = m_totalSimulationSteps;

        // Build filename with step count and simulation info
        std::ostringstream filename;
        filename << "simulation_step_" << m_totalSimulationSteps;

        // Add current simulation name if available
        if (auto* sim = m_simManager.current()) {
            filename << "_" << sim->name();
        }
        filename << ".png";

        // Render to texture first (in case we're in 2D mode)
        if (m_obliqueRenderer) {
            m_obliqueRenderer->renderToTexture(m_terrain, m_colorMapper);

            if (m_obliqueRenderer->exportToPNG(filename.str())) {
                std::cout << "Auto-exported: " << filename.str() << std::endl;
            }
        }
    }
}

} // namespace worldgen
