#include "renderer/ColorMapper.hpp"
#include "utils/Math.hpp"
#include <algorithm>

namespace worldgen {

Color Color::lerp(const Color& a, const Color& b, float t) {
    return {
        static_cast<uint8_t>(worldgen::lerp(static_cast<float>(a.r), static_cast<float>(b.r), t)),
        static_cast<uint8_t>(worldgen::lerp(static_cast<float>(a.g), static_cast<float>(b.g), t)),
        static_cast<uint8_t>(worldgen::lerp(static_cast<float>(a.b), static_cast<float>(b.b), t)),
        static_cast<uint8_t>(worldgen::lerp(static_cast<float>(a.a), static_cast<float>(b.a), t))
    };
}

ColorMapper::ColorMapper() {
    applyPreset(Preset::Terrain);
}

ColorMapper::ColorMapper(Preset preset) {
    applyPreset(preset);
}

void ColorMapper::addStop(float position, Color color) {
    m_stops.push_back({position, color});
    std::sort(m_stops.begin(), m_stops.end(),
        [](const auto& a, const auto& b) { return a.position < b.position; });
}

void ColorMapper::clear() {
    m_stops.clear();
    m_lut.clear();
}

Color ColorMapper::getColor(float height) const {
    if (m_stops.empty()) return {0, 0, 0, 255};
    if (m_stops.size() == 1) return m_stops[0].color;

    height = clamp(height, 0.0f, 1.0f);

    // Find surrounding stops
    for (size_t i = 0; i < m_stops.size() - 1; ++i) {
        if (height >= m_stops[i].position && height <= m_stops[i + 1].position) {
            float t = (height - m_stops[i].position) /
                      (m_stops[i + 1].position - m_stops[i].position);
            return Color::lerp(m_stops[i].color, m_stops[i + 1].color, t);
        }
    }

    return m_stops.back().color;
}

uint32_t ColorMapper::getColorARGB(float height) const {
    return getColor(height).toARGB();
}

Color ColorMapper::getColorWithWater(float height, float waterDepth,
                                      float iceThickness, float snowDepth) const {
    // Priority: ice > snow > water > terrain

    if (iceThickness > 0.01f) {
        // Glacier/ice coloring (white-blue)
        float iceIntensity = clamp(iceThickness * 5.0f, 0.0f, 1.0f);
        return {
            static_cast<uint8_t>(180 + 70 * iceIntensity),
            static_cast<uint8_t>(200 + 50 * iceIntensity),
            static_cast<uint8_t>(230 + 25 * iceIntensity),
            255
        };
    }

    if (snowDepth > 0.005f) {
        // Snow coloring (white with slight blue tint)
        float snowIntensity = clamp(snowDepth * 12.0f, 0.0f, 1.0f);
        return {
            static_cast<uint8_t>(220 + 35 * snowIntensity),
            static_cast<uint8_t>(220 + 35 * snowIntensity),
            static_cast<uint8_t>(230 + 25 * snowIntensity),
            255
        };
    }

    if (waterDepth > 0.005f) {
        // Water color based on depth - high contrast against green
        float depthFactor = clamp(waterDepth * 1.5f, 0.0f, 1.0f);

        // Bright cyan for shallow, deep blue for deep - contrasts well with green
        Color shallowWater = {60, 160, 230, 255};
        Color deepWater = {20, 60, 140, 255};

        return Color::lerp(shallowWater, deepWater, depthFactor);
    }

    // No water/ice/snow - return normal terrain color
    return getColor(height);
}

void ColorMapper::applyPreset(Preset preset) {
    clear();

    switch (preset) {
        case Preset::Terrain:
            // Enhanced natural terrain palette
            addStop(0.00f, {10, 30, 60, 255});      // Deep ocean (dark blue)
            addStop(0.15f, {15, 50, 90, 255});      // Ocean
            addStop(0.25f, {25, 80, 120, 255});     // Mid ocean
            addStop(0.30f, {45, 120, 160, 255});    // Shallow water
            addStop(0.32f, {200, 180, 140, 255});   // Beach (sand)
            addStop(0.36f, {160, 180, 100, 255});   // Coastal grass
            addStop(0.42f, {100, 160, 70, 255});    // Lowland grass
            addStop(0.50f, {70, 135, 55, 255});     // Grass
            addStop(0.58f, {55, 110, 45, 255});     // Forest
            addStop(0.66f, {75, 95, 55, 255});      // Forest edge
            addStop(0.72f, {110, 95, 70, 255});     // Foothills
            addStop(0.78f, {130, 115, 90, 255});    // Mountain
            addStop(0.84f, {150, 140, 125, 255});   // High rock
            addStop(0.90f, {180, 175, 170, 255});   // Alpine
            addStop(0.95f, {220, 220, 225, 255});   // Near snow
            addStop(1.00f, {255, 255, 255, 255});   // Snow
            break;

        case Preset::Grayscale:
            addStop(0.0f, {0, 0, 0, 255});
            addStop(1.0f, {255, 255, 255, 255});
            break;

        case Preset::Elevation:
            addStop(0.0f, {0, 100, 0, 255});      // Low (green)
            addStop(0.5f, {255, 255, 0, 255});   // Mid (yellow)
            addStop(1.0f, {255, 0, 0, 255});     // High (red)
            break;
    }

    buildLUT();
}

void ColorMapper::buildLUT(size_t resolution) {
    m_lut.resize(resolution);
    for (size_t i = 0; i < resolution; ++i) {
        float height = static_cast<float>(i) / (resolution - 1);
        m_lut[i] = getColorARGB(height);
    }
}

uint32_t ColorMapper::lookupFast(float height) const {
    if (m_lut.empty()) return getColorARGB(height);
    size_t index = static_cast<size_t>(clamp(height, 0.0f, 1.0f) * (m_lut.size() - 1));
    return m_lut[index];
}

} // namespace worldgen
