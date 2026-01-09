#pragma once

#include "terrain/TerrainData.hpp"
#include "renderer/ColorMapper.hpp"
#include "renderer/TerrainShader.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace worldgen {

// Software renderer for headless (no-display) image export
// Renders terrain with oblique projection without requiring SDL/OpenGL
class HeadlessRenderer {
public:
    struct Config {
        float heightExaggeration = 100.0f;
        float lightAzimuth = 315.0f;
        float lightElevation = 45.0f;
        bool enableShading = true;
    };

    HeadlessRenderer(size_t width, size_t height);

    // Render terrain to internal pixel buffer
    void render(const TerrainData& terrain, const ColorMapper& colorMapper);

    // Export rendered image to PNG file
    bool exportToPNG(const std::string& filename) const;

    // Configuration
    Config& config() { return m_config; }
    const Config& config() const { return m_config; }

private:
    size_t m_width;
    size_t m_height;
    Config m_config;
    std::vector<uint32_t> m_pixels;

    void setPixel(int x, int y, uint32_t color);
    void clearPixels(uint32_t color = 0xFF000000);

    Vec3f calculateNormal(const float* heightData, size_t x, size_t y,
                          size_t w, size_t h) const;

    static uint32_t applyShading(uint32_t color, float shade);
};

} // namespace worldgen
