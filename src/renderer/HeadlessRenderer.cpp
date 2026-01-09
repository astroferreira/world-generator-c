#include "renderer/HeadlessRenderer.hpp"
#include <algorithm>
#include <cmath>

// Just include header - implementation is in ImageExporter.cpp
#include "stb/stb_image_write.h"

#ifdef WORLDGEN_USE_OPENMP
#include <omp.h>
#endif

namespace worldgen {

HeadlessRenderer::HeadlessRenderer(size_t width, size_t height)
    : m_width(width)
    , m_height(height)
    , m_pixels(width * height, 0xFF000000)
{
}

void HeadlessRenderer::setPixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < static_cast<int>(m_width) &&
        y >= 0 && y < static_cast<int>(m_height)) {
        m_pixels[y * m_width + x] = color;
    }
}

void HeadlessRenderer::clearPixels(uint32_t color) {
    std::fill(m_pixels.begin(), m_pixels.end(), color);
}

Vec3f HeadlessRenderer::calculateNormal(const float* heightData, size_t x, size_t y,
                                         size_t w, size_t h) const {
    size_t x0 = (x > 0) ? x - 1 : 0;
    size_t x1 = (x < w - 1) ? x + 1 : w - 1;
    size_t y0 = (y > 0) ? y - 1 : 0;
    size_t y1 = (y < h - 1) ? y + 1 : h - 1;

    float dzdx = (heightData[y * w + x1] - heightData[y * w + x0]) * 0.5f;
    float dzdy = (heightData[y1 * w + x] - heightData[y0 * w + x]) * 0.5f;

    float scale = 60.0f;
    Vec3f normal(-dzdx * scale, -dzdy * scale, 1.0f);
    return normal.normalized();
}

uint32_t HeadlessRenderer::applyShading(uint32_t color, float shade) {
    shade = std::max(0.0f, std::min(1.0f, shade));

    uint8_t r = static_cast<uint8_t>(((color >> 16) & 0xFF) * shade);
    uint8_t g = static_cast<uint8_t>(((color >> 8) & 0xFF) * shade);
    uint8_t b = static_cast<uint8_t>((color & 0xFF) * shade);

    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

void HeadlessRenderer::render(const TerrainData& terrain, const ColorMapper& colorMapper) {
    render(terrain, colorMapper, nullptr);
}

void HeadlessRenderer::render(const TerrainData& terrain, const ColorMapper& colorMapper,
                               const Heightmap* riverMap) {
    const size_t w = terrain.width();
    const size_t h = terrain.heightDim();

    // Clear to dark blue (ocean background)
    clearPixels(0xFF0A1E3C);

    // Get light direction
    float azimuthRad = m_config.lightAzimuth * 3.14159265f / 180.0f;
    float elevRad = m_config.lightElevation * 3.14159265f / 180.0f;
    Vec3f lightDir(
        std::cos(elevRad) * std::cos(azimuthRad),
        std::cos(elevRad) * std::sin(azimuthRad),
        std::sin(elevRad)
    );

    const float heightExag = m_config.heightExaggeration;
    const float ambientLight = 0.35f;
    const float diffuseLight = 0.65f;
    const bool enableShading = m_config.enableShading;

    const float* heightData = terrain.height->data();
    const float* riverData = riverMap ? riverMap->data() : nullptr;
    const float* iceData = terrain.hydrology ? terrain.hydrology->iceThickness->data() : nullptr;
    const float* snowData = terrain.hydrology ? terrain.hydrology->snowpack->data() : nullptr;

    // River/water color
    const uint8_t waterR = 30, waterG = 100, waterB = 180;

    // Pre-compute shading data
    struct ShadedPixel {
        uint32_t topColor;
        uint32_t sideColor;
        int topY;
        int baseY;
    };
    std::vector<ShadedPixel> shadedData(w * h);

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t mapY = 0; mapY < h; ++mapY) {
        for (size_t x = 0; x < w; ++x) {
            size_t idx = mapY * w + x;
            float height = heightData[idx];

            // Get river intensity from analyzed map (0-1)
            float riverIntensity = riverData ? riverData[idx] : 0.0f;
            float iceThickness = iceData ? iceData[idx] : 0.0f;
            float snowDepth = snowData ? snowData[idx] : 0.0f;

            // Get base terrain color
            Color baseColor = colorMapper.getColor(height);

            // Blend with river color based on intensity
            if (riverIntensity > 0.001f) {
                // Stronger blending: square root makes smaller rivers more visible
                float blend = std::min(1.0f, std::sqrt(riverIntensity) * 2.0f);
                baseColor.r = static_cast<uint8_t>(baseColor.r * (1.0f - blend) + waterR * blend);
                baseColor.g = static_cast<uint8_t>(baseColor.g * (1.0f - blend) + waterG * blend);
                baseColor.b = static_cast<uint8_t>(baseColor.b * (1.0f - blend) + waterB * blend);
            }

            // Apply ice/snow on top
            if (iceThickness > 0.01f) {
                float iceBlend = std::min(1.0f, iceThickness * 3.0f);
                baseColor.r = static_cast<uint8_t>(baseColor.r * (1.0f - iceBlend) + 200 * iceBlend);
                baseColor.g = static_cast<uint8_t>(baseColor.g * (1.0f - iceBlend) + 220 * iceBlend);
                baseColor.b = static_cast<uint8_t>(baseColor.b * (1.0f - iceBlend) + 255 * iceBlend);
            }
            if (snowDepth > 0.01f) {
                float snowBlend = std::min(1.0f, snowDepth * 5.0f);
                baseColor.r = static_cast<uint8_t>(baseColor.r * (1.0f - snowBlend) + 250 * snowBlend);
                baseColor.g = static_cast<uint8_t>(baseColor.g * (1.0f - snowBlend) + 250 * snowBlend);
                baseColor.b = static_cast<uint8_t>(baseColor.b * (1.0f - snowBlend) + 255 * snowBlend);
            }

            uint32_t colorARGB = baseColor.toARGB();

            float shade = 1.0f;
            if (enableShading) {
                Vec3f normal = calculateNormal(heightData, x, mapY, w, h);
                float diffuse = std::max(0.0f, normal.dot(lightDir));
                shade = ambientLight + diffuseLight * diffuse;
            }

            int heightOffset = static_cast<int>(height * heightExag);
            int baseScreenY = static_cast<int>(mapY);
            int topScreenY = std::max(0, baseScreenY - heightOffset);
            baseScreenY = std::min(static_cast<int>(m_height) - 1, baseScreenY);

            shadedData[idx].topColor = applyShading(colorARGB, shade);
            shadedData[idx].sideColor = applyShading(colorARGB, shade * 0.6f);
            shadedData[idx].topY = topScreenY;
            shadedData[idx].baseY = baseScreenY;
        }
    }

    // Render columns (painter's algorithm - back to front)
    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t x = 0; x < w; ++x) {
        int columnDepth = static_cast<int>(m_height);

        for (size_t i = 0; i < h; ++i) {
            size_t mapY = h - 1 - i;
            size_t idx = mapY * w + x;
            const auto& pixel = shadedData[idx];

            int topY = pixel.topY;
            int baseY = pixel.baseY;

            if (topY < columnDepth) {
                m_pixels[topY * m_width + x] = pixel.topColor;

                int sideEnd = std::min(baseY, columnDepth - 1);
                for (int screenY = topY + 1; screenY <= sideEnd; ++screenY) {
                    m_pixels[screenY * m_width + x] = pixel.sideColor;
                }

                columnDepth = topY;
            }
        }
    }
}

bool HeadlessRenderer::exportToPNG(const std::string& filename) const {
    // Convert ARGB to RGB for PNG export
    std::vector<uint8_t> rgb(m_width * m_height * 3);

    for (size_t i = 0; i < m_pixels.size(); ++i) {
        uint32_t pixel = m_pixels[i];
        rgb[i * 3 + 0] = (pixel >> 16) & 0xFF;  // R
        rgb[i * 3 + 1] = (pixel >> 8) & 0xFF;   // G
        rgb[i * 3 + 2] = pixel & 0xFF;          // B
    }

    return stbi_write_png(filename.c_str(),
                          static_cast<int>(m_width),
                          static_cast<int>(m_height),
                          3, rgb.data(),
                          static_cast<int>(m_width * 3)) != 0;
}

} // namespace worldgen
