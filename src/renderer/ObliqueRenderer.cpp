#include "renderer/ObliqueRenderer.hpp"
#include <algorithm>

#ifdef WORLDGEN_USE_OPENMP
#include <omp.h>
#endif

namespace worldgen {

ObliqueRenderer::ObliqueRenderer(SDL_Renderer* renderer, size_t width, size_t height)
    : m_renderer(renderer)
    , m_width(width)
    , m_height(height)
    , m_pixels(width * height, 0xFF000000)
    , m_depthBuffer(width, 0)
    , m_shadedData(width * height)
{
    m_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        static_cast<int>(width),
        static_cast<int>(height)
    );

    // Configure shader for performance
    TerrainShader::Config shaderConfig;
    shaderConfig.heightScale = 60.0f;
    shaderConfig.ambientLight = 0.35f;
    shaderConfig.diffuseLight = 0.65f;
    shaderConfig.aoRadius = 2;  // Reduced for performance
    shaderConfig.aoStrength = 0.3f;
    m_shader = TerrainShader(shaderConfig);
}

ObliqueRenderer::~ObliqueRenderer() {
    if (m_texture) {
        SDL_DestroyTexture(m_texture);
    }
}

inline void ObliqueRenderer::setPixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < static_cast<int>(m_width) &&
        y >= 0 && y < static_cast<int>(m_height)) {
        m_pixels[y * m_width + x] = color;
    }
}

void ObliqueRenderer::clearPixels(uint32_t color) {
    std::fill(m_pixels.begin(), m_pixels.end(), color);
}

Vec3f ObliqueRenderer::calculateNormalFast(const float* heightData, size_t x, size_t y,
                                            size_t w, size_t h) const {
    // Clamp to valid range
    size_t x0 = (x > 0) ? x - 1 : 0;
    size_t x1 = (x < w - 1) ? x + 1 : w - 1;
    size_t y0 = (y > 0) ? y - 1 : 0;
    size_t y1 = (y < h - 1) ? y + 1 : h - 1;

    // Direct array access for speed
    float dzdx = (heightData[y * w + x1] - heightData[y * w + x0]) * 0.5f;
    float dzdy = (heightData[y1 * w + x] - heightData[y0 * w + x]) * 0.5f;

    // Normal vector scaled by height
    float scale = 60.0f;  // heightScale
    Vec3f normal(-dzdx * scale, -dzdy * scale, 1.0f);
    return normal.normalized();
}

void ObliqueRenderer::renderToTexture(const TerrainData& terrain, const ColorMapper& colorMapper) {
    const size_t w = terrain.width();
    const size_t h = terrain.heightDim();

    // Clear to dark blue (ocean background)
    clearPixels(0xFF0A1E3C);

    // Reset depth buffer
    std::fill(m_depthBuffer.begin(), m_depthBuffer.end(), static_cast<int>(m_height));

    // Get light direction once
    Vec3f lightDir = TerrainShader::getLightDirection(m_config.lightAzimuth,
                                                       m_config.lightElevation);

    // Cache parameters
    const float heightExag = m_config.heightExaggeration;
    const float ambientLight = 0.35f;
    const float diffuseLight = 0.65f;
    const bool enableShading = m_config.enableShading;

    // Get data pointers for direct access
    const float* heightData = terrain.height->data();
    const float* waterData = terrain.water ? terrain.water->data() : nullptr;
    const float* iceData = terrain.hydrology ? terrain.hydrology->iceThickness->data() : nullptr;
    const float* snowData = terrain.hydrology ? terrain.hydrology->snowpack->data() : nullptr;

    // Phase 1: Pre-compute shading data in parallel
    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t mapY = 0; mapY < h; ++mapY) {
        for (size_t x = 0; x < w; ++x) {
            size_t idx = mapY * w + x;
            float height = heightData[idx];

            // Get water/ice/snow data
            float waterDepth = waterData ? waterData[idx] : 0.0f;
            float iceThickness = iceData ? iceData[idx] : 0.0f;
            float snowDepth = snowData ? snowData[idx] : 0.0f;

            // Get base color
            Color baseColor = colorMapper.getColorWithWater(height, waterDepth, iceThickness, snowDepth);
            uint32_t colorARGB = baseColor.toARGB();

            // Calculate shading
            float shade = 1.0f;
            if (enableShading) {
                Vec3f normal = calculateNormalFast(heightData, x, mapY, w, h);
                float diffuse = std::max(0.0f, normal.dot(lightDir));
                shade = ambientLight + diffuseLight * diffuse;
            }

            // Pre-compute screen positions
            int heightOffset = static_cast<int>(height * heightExag);
            int baseScreenY = static_cast<int>(mapY);
            int topScreenY = std::max(0, baseScreenY - heightOffset);
            baseScreenY = std::min(static_cast<int>(m_height) - 1, baseScreenY);

            // Store pre-computed data
            m_shadedData[idx].topColor = TerrainShader::applyShading(colorARGB, shade);
            m_shadedData[idx].sideColor = TerrainShader::applyShading(colorARGB, shade * 0.6f);
            m_shadedData[idx].topY = topScreenY;
            m_shadedData[idx].baseY = baseScreenY;
        }
    }

    // Phase 2: Render columns (painter's algorithm - back to front)
    // Process columns independently for better parallelization
    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t x = 0; x < w; ++x) {
        // Each column has its own depth tracking (starts at bottom of screen)
        int columnDepth = static_cast<int>(m_height);

        // Process back to front (large mapY to small mapY) for correct occlusion
        // Back terrain rows (large mapY) appear higher on screen and are drawn first
        // Front terrain rows (small mapY) appear lower and can occlude the back
        for (size_t i = 0; i < h; ++i) {
            size_t mapY = h - 1 - i;  // Iterate from back (h-1) to front (0)
            size_t idx = mapY * w + x;
            const auto& pixel = m_shadedData[idx];

            int topY = pixel.topY;
            int baseY = pixel.baseY;

            // Draw if this terrain cell reaches higher than anything we've drawn
            if (topY < columnDepth) {
                m_pixels[topY * m_width + x] = pixel.topColor;

                // Draw vertical side (from top+1 to min of baseY or columnDepth)
                int sideEnd = std::min(baseY, columnDepth - 1);
                for (int screenY = topY + 1; screenY <= sideEnd; ++screenY) {
                    m_pixels[screenY * m_width + x] = pixel.sideColor;
                }

                // Update depth - nothing below topY needs to be drawn in this column
                columnDepth = topY;
            }
        }
    }

    // Upload to texture
    SDL_UpdateTexture(m_texture, nullptr, m_pixels.data(),
                      static_cast<int>(m_width * sizeof(uint32_t)));
}

void ObliqueRenderer::render(const TerrainData& terrain, const ColorMapper& colorMapper) {
    renderToTexture(terrain, colorMapper);
}

void ObliqueRenderer::display(const SDL_Rect& destRect) {
    SDL_RenderCopy(m_renderer, m_texture, nullptr, &destRect);
}

} // namespace worldgen
