#include "core/Application.hpp"
#include "renderer/HeadlessRenderer.hpp"
#include "generation/TerrainGenerator.hpp"
#include "simulation/SimulationManager.hpp"
#include "simulation/ThermalErosion.hpp"
#include "simulation/HydrologySimulation.hpp"
#include "simulation/GlacierSystem.hpp"
#include "simulation/HydraulicErosion.hpp"
#include "analysis/RiverMapper.hpp"
#include "renderer/ColorMapper.hpp"
#include "stb/stb_image_write.h"
#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <vector>
#include <algorithm>

// Export a heightmap as grayscale PNG (0=black, 1=white)
bool exportHeightmapToPNG(const worldgen::Heightmap& map, const std::string& filename) {
    const size_t w = map.width();
    const size_t h = map.height();
    const float* data = map.data();

    std::vector<uint8_t> pixels(w * h);

    // Find max value for normalization
    float maxVal = 0.0f;
    for (size_t i = 0; i < w * h; ++i) {
        maxVal = std::max(maxVal, data[i]);
    }
    if (maxVal < 0.001f) maxVal = 1.0f;

    // Convert to grayscale
    for (size_t i = 0; i < w * h; ++i) {
        float normalized = std::min(1.0f, data[i] / maxVal);
        pixels[i] = static_cast<uint8_t>(normalized * 255.0f);
    }

    return stbi_write_png(filename.c_str(),
                          static_cast<int>(w),
                          static_cast<int>(h),
                          1, pixels.data(),
                          static_cast<int>(w)) != 0;
}

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

    // Setup river mapper for terrain-based water visualization
    worldgen::RiverMapper riverMapper;

    // Helper lambda to render with river analysis
    auto renderWithRivers = [&](const std::string& filename) {
        // Analyze terrain to generate river map (uses flow accumulation + valley detection)
        auto riverMap = riverMapper.generateWaterMap(*terrain.height);
        renderer.render(terrain, colorMapper, riverMap.get());
        renderer.exportToPNG(filename);
    };

    // Export initial state
    renderWithRivers("simulation_step_0_initial.png");
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

            renderWithRivers(filename.str());
            std::cout << "\nExported: " << filename.str() << std::endl;
        }
    }

    // Final export with analyzed river map
    std::cout << "\n\nSimulation complete!" << std::endl;
    std::cout << "Analyzing terrain for river visualization..." << std::endl;
    renderWithRivers("simulation_final.png");
    std::cout << "Exported: simulation_final.png" << std::endl;

    // Export standalone river map (2D grayscale - rivers only, no terrain)
    std::cout << "Generating standalone water maps..." << std::endl;
    auto finalRiverMap = riverMapper.generateRiverMap(*terrain.height);
    auto finalLakeMap = riverMapper.generateLakeMap(*terrain.height);
    auto finalWaterMap = riverMapper.generateWaterMap(*terrain.height);

    exportHeightmapToPNG(*finalRiverMap, "river_map_2d.png");
    exportHeightmapToPNG(*finalLakeMap, "lake_map_2d.png");
    exportHeightmapToPNG(*finalWaterMap, "water_map_2d.png");
    std::cout << "Exported: river_map_2d.png, lake_map_2d.png, water_map_2d.png" << std::endl;

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
