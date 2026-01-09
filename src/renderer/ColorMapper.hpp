#pragma once

#include <vector>
#include <cstdint>

namespace worldgen {

struct Color {
    uint8_t r, g, b, a;

    uint32_t toARGB() const {
        return (a << 24) | (r << 16) | (g << 8) | b;
    }

    static Color lerp(const Color& a, const Color& b, float t);
};

class ColorMapper {
public:
    enum class Preset { Grayscale, Terrain, Elevation };

    ColorMapper();
    explicit ColorMapper(Preset preset);

    void addStop(float position, Color color);
    void clear();

    Color getColor(float height) const;
    uint32_t getColorARGB(float height) const;

    // Water-aware color mapping
    Color getColorWithWater(float height, float waterDepth,
                            float iceThickness = 0.0f,
                            float snowDepth = 0.0f) const;

    void applyPreset(Preset preset);

    void buildLUT(size_t resolution = 4096);
    uint32_t lookupFast(float height) const;

private:
    struct GradientStop {
        float position;
        Color color;
    };
    std::vector<GradientStop> m_stops;
    std::vector<uint32_t> m_lut;
};

} // namespace worldgen
