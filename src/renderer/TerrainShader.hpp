#pragma once

#include "terrain/Heightmap.hpp"
#include <cmath>
#include <algorithm>

namespace worldgen {

// Simple 3D vector for normal calculations
struct Vec3f {
    float x, y, z;

    Vec3f(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}

    float length() const {
        return std::sqrt(x*x + y*y + z*z);
    }

    Vec3f normalized() const {
        float len = length();
        if (len < 0.0001f) return {0, 0, 1};
        return {x/len, y/len, z/len};
    }

    float dot(const Vec3f& other) const {
        return x*other.x + y*other.y + z*other.z;
    }
};

// Terrain shading utilities for hillshading and ambient occlusion
class TerrainShader {
public:
    // Configuration
    struct Config {
        float heightScale = 50.0f;      // How much height affects normals
        float ambientLight = 0.35f;     // Minimum light level
        float diffuseLight = 0.65f;     // Directional light strength
        int aoRadius = 3;               // Ambient occlusion sample radius
        float aoStrength = 0.4f;        // Ambient occlusion intensity
    };

    TerrainShader() = default;
    explicit TerrainShader(const Config& config) : m_config(config) {}

    // Calculate surface normal from heightmap gradients using central differences
    Vec3f calculateNormal(const Heightmap& heightmap, size_t x, size_t y) const;

    // Calculate hillshading intensity (0-1) from normal and light direction
    float calculateHillshade(const Vec3f& normal, const Vec3f& lightDir) const;

    // Calculate ambient occlusion factor (0-1, 1 = no occlusion)
    float calculateAO(const Heightmap& heightmap, size_t x, size_t y) const;

    // Combined shading: hillshade * AO
    float calculateShading(const Heightmap& heightmap, size_t x, size_t y,
                          const Vec3f& lightDir) const;

    // Apply shading to a color (multiply RGB by shade factor)
    static uint32_t applyShading(uint32_t color, float shade);

    // Get light direction from angle (degrees, 0 = east, 90 = north)
    static Vec3f getLightDirection(float azimuth, float elevation = 45.0f);

    Config& config() { return m_config; }
    const Config& config() const { return m_config; }

private:
    Config m_config;
};

} // namespace worldgen
