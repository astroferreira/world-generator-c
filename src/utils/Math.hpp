#pragma once

#include <algorithm>
#include <cmath>

namespace worldgen {

template<typename T>
constexpr T lerp(T a, T b, T t) {
    return a + t * (b - a);
}

template<typename T>
constexpr T clamp(T value, T min, T max) {
    return std::max(min, std::min(value, max));
}

template<typename T>
constexpr T smoothstep(T edge0, T edge1, T x) {
    T t = clamp((x - edge0) / (edge1 - edge0), T(0), T(1));
    return t * t * (T(3) - T(2) * t);
}

template<typename T>
constexpr T remap(T value, T inMin, T inMax, T outMin, T outMax) {
    return outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin);
}

} // namespace worldgen
