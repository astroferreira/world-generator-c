#pragma once

#include "terrain/TerrainData.hpp"
#include "renderer/ColorMapper.hpp"
#include "renderer/TerrainShader.hpp"
#include <SDL2/SDL.h>
#include <vector>

namespace worldgen {

// Renders terrain with oblique projection for 2.5D effect
class ObliqueRenderer {
public:
    struct Config {
        float heightExaggeration = 100.0f;  // Vertical scale for height displacement
        float lightAzimuth = 315.0f;        // Light direction (315 = northwest)
        float lightElevation = 45.0f;       // Light elevation angle
        bool enableShading = true;
        bool enableAO = false;               // Disable by default for performance
        int aoRadius = 2;                    // Reduced AO radius for performance
    };

    ObliqueRenderer(SDL_Renderer* renderer, size_t width, size_t height);
    ~ObliqueRenderer();

    // Render terrain with oblique projection and shading
    void render(const TerrainData& terrain, const ColorMapper& colorMapper);

    // Render to SDL texture (for zoom/pan support)
    void renderToTexture(const TerrainData& terrain, const ColorMapper& colorMapper);

    // Display the rendered texture
    void display(const SDL_Rect& destRect);

    // Export pixel buffer to PNG file
    bool exportToPNG(const std::string& filename) const;

    // Configuration
    Config& config() { return m_config; }
    const Config& config() const { return m_config; }

    // Rotate light direction
    void rotateLight(float degrees) { m_config.lightAzimuth += degrees; }

private:
    SDL_Renderer* m_renderer;
    SDL_Texture* m_texture;
    size_t m_width;
    size_t m_height;
    Config m_config;
    TerrainShader m_shader;

    // Pixel buffer for software rendering
    std::vector<uint32_t> m_pixels;

    // Depth buffer for z-ordering (per-column max Y)
    std::vector<int> m_depthBuffer;

    // Pre-computed shading data per terrain cell
    struct ShadedPixel {
        uint32_t topColor;
        uint32_t sideColor;
        int topY;
        int baseY;
    };
    std::vector<ShadedPixel> m_shadedData;

    void setPixel(int x, int y, uint32_t color);
    void clearPixels(uint32_t color = 0xFF000000);

    // Fast normal calculation without bounds checking
    Vec3f calculateNormalFast(const float* heightData, size_t x, size_t y,
                               size_t w, size_t h) const;
};

} // namespace worldgen
