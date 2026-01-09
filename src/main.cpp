#include "core/Application.hpp"
#include "renderer/HeadlessRenderer.hpp"
#include "generation/TerrainGenerator.hpp"
#include "simulation/SimulationManager.hpp"
#include "simulation/ThermalErosion.hpp"
#include "simulation/HydrologySimulation.hpp"
#include "simulation/GlacierSystem.hpp"
#include "simulation/HydraulicErosion.hpp"
#include "renderer/ColorMapper.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <sstream>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --headless         Run in headless mode (no display)\n";
    std::cout << "  --steps N          Number of simulation steps (default: 500)\n";
    std::cout << "  --export-interval N Export image every N steps (default: 50)\n";
    std::cout << "  --seed N           Random seed (default: 42)\n";
    std::cout << "  --size N           Terrain size NxN (default: 1024 in headless, 2048 otherwise)\n";
    std::cout << "  --help             Show this help message\n";
    std::cout << "\nInteractive mode controls:\n";
    std::cout << "  Space - Start/pause simulation\n";
    std::cout << "  R     - Regenerate terrain (new seed)\n";
    std::cout << "  S     - Single simulation step\n";
    std::cout << "  E     - Export to terrain.png\n";
    std::cout << "  V     - Toggle 2D/3D view\n";
    std::cout << "  G     - Toggle grayscale view\n";
    std::cout << "  W     - Toggle water/ice/snow rendering\n";
    std::cout << "  Q/Esc - Quit\n";
}

int runHeadless(int steps, int exportInterval, int seed, int size) {
    std::cout << "=== Headless Batch Export Mode ===" << std::endl;
    std::cout << "Terrain size: " << size << "x" << size << std::endl;
    std::cout << "Seed: " << seed << std::endl;
    std::cout << "Total steps: " << steps << std::endl;
    std::cout << "Export interval: " << exportInterval << " steps" << std::endl;
    std::cout << std::endl;

    // Generate terrain
    std::cout << "Generating terrain..." << std::endl;
    worldgen::TerrainGenConfig genConfig;
    genConfig.width = size;
    genConfig.height = size;
    genConfig.seed = seed;

    worldgen::TerrainGenerator generator(genConfig);
    worldgen::TerrainData terrain = generator.generate([](float progress, const char* stage) {
        std::cout << "  " << stage << " (" << static_cast<int>(progress * 100) << "%)" << std::endl;
    });

    // Setup color mapper
    worldgen::ColorMapper colorMapper;
    colorMapper.applyPreset(worldgen::ColorMapper::Preset::Terrain);
    colorMapper.buildLUT(4096);

    // Setup headless renderer
    worldgen::HeadlessRenderer renderer(size, size);

    // Setup simulations
    worldgen::SimulationManager simManager;
    simManager.addSimulation(std::make_unique<worldgen::ThermalErosion>());
    simManager.addSimulation(std::make_unique<worldgen::HydrologySimulation>());
    simManager.addSimulation(std::make_unique<worldgen::GlacierSystem>());
    simManager.addSimulation(std::make_unique<worldgen::HydraulicErosion>());
    simManager.initialize(terrain);

    std::cout << "\nStarting simulation..." << std::endl;

    // Export initial state
    renderer.render(terrain, colorMapper);
    renderer.exportToPNG("simulation_step_0_initial.png");
    std::cout << "Exported: simulation_step_0_initial.png" << std::endl;

    // Run simulation
    int lastExport = 0;
    for (int step = 1; step <= steps && !simManager.isComplete(); ++step) {
        simManager.step();

        // Progress output
        if (step % 10 == 0) {
            auto* sim = simManager.current();
            std::string simName = sim ? sim->name() : "Complete";
            std::cout << "\rStep " << step << "/" << steps << " - " << simName
                      << " (iter: " << (sim ? sim->iterations() : 0) << ")   " << std::flush;
        }

        // Export at intervals
        if (step - lastExport >= exportInterval) {
            lastExport = step;

            std::ostringstream filename;
            filename << "simulation_step_" << step;
            if (auto* sim = simManager.current()) {
                filename << "_" << sim->name();
            }
            filename << ".png";

            renderer.render(terrain, colorMapper);
            renderer.exportToPNG(filename.str());
            std::cout << "\nExported: " << filename.str() << std::endl;
        }
    }

    // Final export
    std::cout << "\n\nSimulation complete!" << std::endl;
    renderer.render(terrain, colorMapper);
    renderer.exportToPNG("simulation_final.png");
    std::cout << "Exported: simulation_final.png" << std::endl;

    return 0;
}

int main(int argc, char* argv[]) {
    bool headless = false;
    int steps = 500;
    int exportInterval = 50;
    int seed = 42;
    int size = 0;  // 0 means use default

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            headless = true;
        } else if (std::strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
            steps = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--export-interval") == 0 && i + 1 < argc) {
            exportInterval = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            size = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
    }

    std::cout << "World Generator v1.0" << std::endl;
    std::cout << std::endl;

    if (headless) {
        if (size == 0) size = 1024;  // Default for headless
        return runHeadless(steps, exportInterval, seed, size);
    }

    // Interactive mode
    if (size == 0) size = 2048;  // Default for interactive

    worldgen::AppConfig config;
    config.windowWidth = 1024;
    config.windowHeight = 1024;
    config.terrainWidth = size;
    config.terrainHeight = size;
    config.simulationStepsPerFrame = 1;

    worldgen::Application app(config);

    if (!app.initialize()) {
        std::cerr << "Failed to initialize application" << std::endl;
        return 1;
    }

    app.run();

    return 0;
}
