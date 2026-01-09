#include "core/Application.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::cout << "World Generator v1.0" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  Space - Start/pause simulation" << std::endl;
    std::cout << "  R     - Regenerate terrain (new seed)" << std::endl;
    std::cout << "  S     - Single simulation step" << std::endl;
    std::cout << "  E     - Export to terrain.png" << std::endl;
    std::cout << "  G     - Toggle grayscale view" << std::endl;
    std::cout << "  W     - Toggle water/ice/snow rendering" << std::endl;
    std::cout << "  Q/Esc - Quit" << std::endl;
    std::cout << std::endl;

    worldgen::AppConfig config;
    config.windowWidth = 1024;
    config.windowHeight = 1024;
    config.terrainWidth = 2048;
    config.terrainHeight = 2048;
    config.simulationStepsPerFrame = 1;

    worldgen::Application app(config);

    if (!app.initialize()) {
        std::cerr << "Failed to initialize application" << std::endl;
        return 1;
    }

    app.run();

    return 0;
}
