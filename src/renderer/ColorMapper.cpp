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

void ColorMapper::applyPreset(Preset preset) {
    clear();

    switch (preset) {
        case Preset::Terrain:
            addStop(0.00f, {0, 0, 50, 255});       // Deep ocean
            addStop(0.20f, {0, 50, 150, 255});     // Ocean
            addStop(0.30f, {50, 150, 200, 255});   // Shallow water
            addStop(0.32f, {240, 220, 150, 255}); // Beach
            addStop(0.40f, {50, 150, 50, 255});   // Grass
            addStop(0.55f, {30, 100, 30, 255});   // Forest
            addStop(0.70f, {100, 80, 60, 255});   // Mountain rock
            addStop(0.85f, {120, 120, 120, 255}); // High rock
            addStop(1.00f, {255, 255, 255, 255}); // Snow
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
