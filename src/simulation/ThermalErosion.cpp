#include "simulation/ThermalErosion.hpp"
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

    const size_t totalSize = terrain.width() * terrain.heightDim();
    m_deltaBuffer.resize(totalSize, 0.0f);

#ifdef WORLDGEN_USE_OPENMP
    // Pre-allocate thread-local buffers
    int maxThreads = omp_get_max_threads();
    m_threadLocalDeltas.resize(maxThreads);
    for (auto& buffer : m_threadLocalDeltas) {
        buffer.resize(totalSize, 0.0f);
    }
#endif
}

void ThermalErosion::step() {
    if (!m_terrain) return;

    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();
    const size_t totalSize = w * h;

    // 8-connected neighbors (4-connected starts at index 1)
    static constexpr int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
    static constexpr float dist[] = {1.414f, 1.0f, 1.414f, 1.0f, 1.0f, 1.414f, 1.0f, 1.414f};

    const int neighborCount = m_params.use8Neighbors ? 8 : 4;
    const int neighborOffset = m_params.use8Neighbors ? 0 : 1;
    const float talusAngle = m_params.talusAngle;
    const float erosionRate = m_params.erosionRate;

    int changes = 0;
    float* heightData = m_terrain->height->data();

#ifdef WORLDGEN_USE_OPENMP
    // Clear thread-local buffers
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        std::fill(m_threadLocalDeltas[tid].begin(), m_threadLocalDeltas[tid].end(), 0.0f);
    }

    // Main erosion pass with thread-local accumulation (no atomics)
    #pragma omp parallel reduction(+:changes)
    {
        int tid = omp_get_thread_num();
        float* localDelta = m_threadLocalDeltas[tid].data();

        #pragma omp for schedule(static)
        for (size_t y = 1; y < h - 1; ++y) {
            for (size_t x = 1; x < w - 1; ++x) {
                const size_t idx = y * w + x;
                const float centerHeight = heightData[idx];

                float totalExcess = 0.0f;
                int unstableCount = 0;

                // First pass: calculate total excess slope
                for (int i = neighborOffset; i < neighborCount; ++i) {
                    const int nx = static_cast<int>(x) + dx[i];
                    const int ny = static_cast<int>(y) + dy[i];
                    const float neighborHeight = heightData[ny * w + nx];
                    const float heightDiff = centerHeight - neighborHeight;
                    const float slope = heightDiff / dist[i];

                    if (slope > talusAngle) {
                        totalExcess += slope - talusAngle;
                        ++unstableCount;
                    }
                }

                if (unstableCount == 0) continue;
                ++changes;

                // Second pass: distribute material to thread-local buffer
                for (int i = neighborOffset; i < neighborCount; ++i) {
                    const int nx = static_cast<int>(x) + dx[i];
                    const int ny = static_cast<int>(y) + dy[i];
                    const size_t nidx = ny * w + nx;
                    const float neighborHeight = heightData[nidx];
                    const float heightDiff = centerHeight - neighborHeight;
                    const float slope = heightDiff / dist[i];

                    if (slope > talusAngle) {
                        const float excessSlope = slope - talusAngle;
                        const float proportion = excessSlope / totalExcess;
                        const float transfer = heightDiff * 0.5f * proportion * erosionRate;

                        localDelta[idx] -= transfer;
                        localDelta[nidx] += transfer;
                    }
                }
            }
        }
    }

    // Reduce all thread-local buffers into heightmap
    const int numThreads = static_cast<int>(m_threadLocalDeltas.size());
    #pragma omp parallel for schedule(static)
    for (size_t i = 0; i < totalSize; ++i) {
        float sum = 0.0f;
        for (int t = 0; t < numThreads; ++t) {
            sum += m_threadLocalDeltas[t][i];
        }
        heightData[i] += sum;
    }

#else
    // Sequential version
    std::fill(m_deltaBuffer.begin(), m_deltaBuffer.end(), 0.0f);

    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            const size_t idx = y * w + x;
            const float centerHeight = heightData[idx];

            float totalExcess = 0.0f;
            int unstableCount = 0;

            for (int i = neighborOffset; i < neighborCount; ++i) {
                const int nx = static_cast<int>(x) + dx[i];
                const int ny = static_cast<int>(y) + dy[i];
                const float neighborHeight = heightData[ny * w + nx];
                const float heightDiff = centerHeight - neighborHeight;
                const float slope = heightDiff / dist[i];

                if (slope > talusAngle) {
                    totalExcess += slope - talusAngle;
                    ++unstableCount;
                }
            }

            if (unstableCount == 0) continue;
            ++changes;

            for (int i = neighborOffset; i < neighborCount; ++i) {
                const int nx = static_cast<int>(x) + dx[i];
                const int ny = static_cast<int>(y) + dy[i];
                const size_t nidx = ny * w + nx;
                const float neighborHeight = heightData[nidx];
                const float heightDiff = centerHeight - neighborHeight;
                const float slope = heightDiff / dist[i];

                if (slope > talusAngle) {
                    const float excessSlope = slope - talusAngle;
                    const float proportion = excessSlope / totalExcess;
                    const float transfer = heightDiff * 0.5f * proportion * erosionRate;

                    m_deltaBuffer[idx] -= transfer;
                    m_deltaBuffer[nidx] += transfer;
                }
            }
        }
    }

    // Apply changes
    for (size_t i = 0; i < totalSize; ++i) {
        heightData[i] += m_deltaBuffer[i];
    }
#endif

    m_changesLastStep = changes;
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
