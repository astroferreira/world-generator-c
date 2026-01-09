#include "renderer/TerrainShader.hpp"
#include <cmath>

namespace worldgen {

Vec3f TerrainShader::calculateNormal(const Heightmap& heightmap, size_t x, size_t y) const {
    const size_t w = heightmap.width();
    const size_t h = heightmap.height();

    // Clamp to valid range
    size_t x0 = (x > 0) ? x - 1 : 0;
    size_t x1 = (x < w - 1) ? x + 1 : w - 1;
    size_t y0 = (y > 0) ? y - 1 : 0;
    size_t y1 = (y < h - 1) ? y + 1 : h - 1;

    // Calculate gradients using central differences
    float dzdx = (heightmap.get(x1, y) - heightmap.get(x0, y)) * 0.5f;
    float dzdy = (heightmap.get(x, y1) - heightmap.get(x, y0)) * 0.5f;

    // Normal vector: (-dz/dx, -dz/dy, 1) scaled by height
    Vec3f normal(-dzdx * m_config.heightScale,
                 -dzdy * m_config.heightScale,
                 1.0f);

    return normal.normalized();
}

float TerrainShader::calculateHillshade(const Vec3f& normal, const Vec3f& lightDir) const {
    // Lambertian diffuse shading
    float diffuse = std::max(0.0f, normal.dot(lightDir));

    // Combine ambient and diffuse
    return m_config.ambientLight + m_config.diffuseLight * diffuse;
}

float TerrainShader::calculateAO(const Heightmap& heightmap, size_t x, size_t y) const {
    const size_t w = heightmap.width();
    const size_t h = heightmap.height();
    const int radius = m_config.aoRadius;

    float centerHeight = heightmap.get(x, y);
    float occlusion = 0.0f;
    int samples = 0;

    // Sample surrounding heights
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx == 0 && dy == 0) continue;

            int nx = static_cast<int>(x) + dx;
            int ny = static_cast<int>(y) + dy;

            if (nx < 0 || nx >= static_cast<int>(w) ||
                ny < 0 || ny >= static_cast<int>(h)) continue;

            float neighborHeight = heightmap.get(nx, ny);
            float dist = std::sqrt(static_cast<float>(dx*dx + dy*dy));

            // Higher neighbors occlude this point
            if (neighborHeight > centerHeight) {
                float heightDiff = neighborHeight - centerHeight;
                // Occlusion falls off with distance
                occlusion += (heightDiff / dist) * m_config.aoStrength;
            }

            samples++;
        }
    }

    if (samples == 0) return 1.0f;

    // Normalize and convert to brightness (1 = no occlusion, 0 = full occlusion)
    occlusion = occlusion / samples;
    return std::max(0.3f, 1.0f - occlusion * 5.0f);
}

float TerrainShader::calculateShading(const Heightmap& heightmap, size_t x, size_t y,
                                       const Vec3f& lightDir) const {
    Vec3f normal = calculateNormal(heightmap, x, y);
    float hillshade = calculateHillshade(normal, lightDir);
    float ao = calculateAO(heightmap, x, y);

    return hillshade * ao;
}

uint32_t TerrainShader::applyShading(uint32_t color, float shade) {
    // Extract ARGB components
    uint8_t a = (color >> 24) & 0xFF;
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    // Apply shading to RGB
    shade = std::max(0.0f, std::min(1.0f, shade));
    r = static_cast<uint8_t>(r * shade);
    g = static_cast<uint8_t>(g * shade);
    b = static_cast<uint8_t>(b * shade);

    return (a << 24) | (r << 16) | (g << 8) | b;
}

Vec3f TerrainShader::getLightDirection(float azimuth, float elevation) {
    // Convert to radians
    float azimuthRad = azimuth * 3.14159265f / 180.0f;
    float elevationRad = elevation * 3.14159265f / 180.0f;

    // Calculate light direction vector
    // Azimuth: 0 = east, 90 = north, 180 = west, 270 = south
    float cosElev = std::cos(elevationRad);
    return Vec3f(
        std::cos(azimuthRad) * cosElev,
        std::sin(azimuthRad) * cosElev,
        std::sin(elevationRad)
    ).normalized();
}

} // namespace worldgen
