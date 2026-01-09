#include "simulation/ThermalErosion.hpp"
#include <cmath>
#include <algorithm>

#ifdef WORLDGEN_USE_OPENMP
#include <omp.h>
#endif

namespace worldgen {

ThermalErosion::ThermalErosion() = default;

ThermalErosion::ThermalErosion(const ThermalParams& params)
    : m_params(params)
{
}

void ThermalErosion::initialize(TerrainData& terrain) {
    m_terrain = &terrain;
    m_iterations = 0;
    m_changesLastStep = 0;
    m_deltaBuffer.resize(terrain.width() * terrain.heightDim(), 0.0f);
}

void ThermalErosion::step() {
    if (!m_terrain) return;

    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    std::fill(m_deltaBuffer.begin(), m_deltaBuffer.end(), 0.0f);
    m_changesLastStep = 0;

    // 8-connected neighbors
    const int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
    const int neighborCount = m_params.use8Neighbors ? 8 : 4;
    const int neighborOffset = m_params.use8Neighbors ? 0 : 1;

    int changes = 0;

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for reduction(+:changes)
    #endif
    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            float centerHeight = m_terrain->height->get(x, y);

            float totalExcess = 0.0f;
            int unstableCount = 0;

            // First pass: calculate total excess slope
            for (int i = neighborOffset; i < neighborCount; ++i) {
                int nx = static_cast<int>(x) + dx[i];
                int ny = static_cast<int>(y) + dy[i];

                float neighborHeight = m_terrain->height->get(nx, ny);
                float heightDiff = centerHeight - neighborHeight;
                float dist = (dx[i] != 0 && dy[i] != 0) ? 1.414f : 1.0f;
                float slope = heightDiff / dist;

                if (slope > m_params.talusAngle) {
                    totalExcess += slope - m_params.talusAngle;
                    ++unstableCount;
                }
            }

            if (unstableCount == 0) continue;
            ++changes;

            // Second pass: distribute material
            for (int i = neighborOffset; i < neighborCount; ++i) {
                int nx = static_cast<int>(x) + dx[i];
                int ny = static_cast<int>(y) + dy[i];

                float neighborHeight = m_terrain->height->get(nx, ny);
                float heightDiff = centerHeight - neighborHeight;
                float dist = (dx[i] != 0 && dy[i] != 0) ? 1.414f : 1.0f;
                float slope = heightDiff / dist;

                if (slope > m_params.talusAngle) {
                    float excessSlope = slope - m_params.talusAngle;
                    float proportion = excessSlope / totalExcess;
                    float transfer = heightDiff * 0.5f * proportion * m_params.erosionRate;

                    #ifdef WORLDGEN_USE_OPENMP
                    #pragma omp atomic
                    #endif
                    m_deltaBuffer[y * w + x] -= transfer;
                    #ifdef WORLDGEN_USE_OPENMP
                    #pragma omp atomic
                    #endif
                    m_deltaBuffer[ny * w + nx] += transfer;
                }
            }
        }
    }

    m_changesLastStep = changes;

    // Apply changes
    for (size_t i = 0; i < w * h; ++i) {
        m_terrain->height->data()[i] += m_deltaBuffer[i];
    }

    ++m_iterations;
}

void ThermalErosion::reset() {
    m_iterations = 0;
    m_changesLastStep = 0;
}

void ThermalErosion::setParams(const ThermalParams& params) {
    m_params = params;
}

} // namespace worldgen
