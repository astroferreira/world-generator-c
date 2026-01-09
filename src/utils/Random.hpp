#pragma once

#include <random>
#include <cstdint>

namespace worldgen {

class Random {
public:
    Random() : m_engine(std::random_device{}()) {}
    explicit Random(uint32_t seed) : m_engine(seed) {}

    void seed(uint32_t seed) { m_engine.seed(seed); }

    // Random integer in [min, max]
    int nextInt(int min, int max) {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(m_engine);
    }

    // Random float in [min, max)
    float nextFloat(float min, float max) {
        std::uniform_real_distribution<float> dist(min, max);
        return dist(m_engine);
    }

    // Random float in [0, 1)
    float nextFloat() {
        return nextFloat(0.0f, 1.0f);
    }

    // Random double in [0, 1)
    double nextDouble() {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(m_engine);
    }

private:
    std::mt19937 m_engine;
};

} // namespace worldgen
