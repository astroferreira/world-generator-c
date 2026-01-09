#include "terrain/Heightmap.hpp"
#include "utils/Math.hpp"
#include <algorithm>
#include <numeric>

namespace worldgen {

Heightmap::Heightmap(size_t width, size_t height)
    : m_width(width)
    , m_height(height)
    , m_data(width * height, 0.0f)
{
}

Heightmap::ValueType Heightmap::get(size_t x, size_t y) const {
    return m_data[index(x, y)];
}

Heightmap::ValueType& Heightmap::at(size_t x, size_t y) {
    return m_data[index(x, y)];
}

Heightmap::ValueType Heightmap::getInterpolated(float x, float y) const {
    // Bilinear interpolation
    size_t x0 = static_cast<size_t>(x);
    size_t y0 = static_cast<size_t>(y);
    size_t x1 = std::min(x0 + 1, m_width - 1);
    size_t y1 = std::min(y0 + 1, m_height - 1);

    float fx = x - x0;
    float fy = y - y0;

    ValueType v00 = get(x0, y0);
    ValueType v10 = get(x1, y0);
    ValueType v01 = get(x0, y1);
    ValueType v11 = get(x1, y1);

    ValueType v0 = lerp(v00, v10, fx);
    ValueType v1 = lerp(v01, v11, fx);

    return lerp(v0, v1, fy);
}

Vec2f Heightmap::getGradient(size_t x, size_t y) const {
    // Central difference for interior points, forward/backward at edges
    float gx, gy;

    if (x == 0) {
        gx = get(x + 1, y) - get(x, y);
    } else if (x == m_width - 1) {
        gx = get(x, y) - get(x - 1, y);
    } else {
        gx = (get(x + 1, y) - get(x - 1, y)) * 0.5f;
    }

    if (y == 0) {
        gy = get(x, y + 1) - get(x, y);
    } else if (y == m_height - 1) {
        gy = get(x, y) - get(x, y - 1);
    } else {
        gy = (get(x, y + 1) - get(x, y - 1)) * 0.5f;
    }

    return {gx, gy};
}

void Heightmap::set(size_t x, size_t y, ValueType value) {
    m_data[index(x, y)] = value;
}

void Heightmap::add(size_t x, size_t y, ValueType delta) {
    m_data[index(x, y)] += delta;
}

void Heightmap::fill(ValueType value) {
    std::fill(m_data.begin(), m_data.end(), value);
}

void Heightmap::normalize() {
    ValueType minVal = min();
    ValueType maxVal = max();
    ValueType range = maxVal - minVal;

    if (range > 0.0001f) {
        for (auto& v : m_data) {
            v = (v - minVal) / range;
        }
    }
}

Heightmap::ValueType Heightmap::min() const {
    return *std::min_element(m_data.begin(), m_data.end());
}

Heightmap::ValueType Heightmap::max() const {
    return *std::max_element(m_data.begin(), m_data.end());
}

Heightmap::ValueType Heightmap::average() const {
    return std::accumulate(m_data.begin(), m_data.end(), 0.0f) / m_data.size();
}

} // namespace worldgen
