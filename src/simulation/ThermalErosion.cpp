#include "simulation/ThermalErosion.hpp"
#include "utils/Profiler.hpp"
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
    PROFILE_SCOPE("ThermalErosion::step");
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
    const int maxTransferNeighbors = m_params.maxTransferNeighbors;

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

        // Thread-local storage for top N steepest neighbors (indices and data)
        size_t topIndices[8];
        float topSlopes[8];
        float topHeightDiffs[8];

        #pragma omp for schedule(static)
        for (size_t y = 1; y < h - 1; ++y) {
            for (size_t x = 1; x < w - 1; ++x) {
                const size_t idx = y * w + x;
                const float centerHeight = heightData[idx];

                int unstableCount = 0;

                // Single pass: collect unstable neighbors
                for (int i = neighborOffset; i < neighborCount; ++i) {
                    const int nx = static_cast<int>(x) + dx[i];
                    const int ny = static_cast<int>(y) + dy[i];
                    const size_t nidx = ny * w + nx;
                    const float neighborHeight = heightData[nidx];
                    const float heightDiff = centerHeight - neighborHeight;
                    const float slope = heightDiff / dist[i];

                    if (slope > talusAngle) {
                        topIndices[unstableCount] = nidx;
                        topSlopes[unstableCount] = slope;
                        topHeightDiffs[unstableCount] = heightDiff;
                        ++unstableCount;
                    }
                }

                if (unstableCount == 0) continue;
                ++changes;

                // Find top N steepest using partial selection (O(n*k) but k is small)
                int transferCount = std::min(unstableCount, maxTransferNeighbors);

                // Simple selection: for each position, find the max remaining
                for (int k = 0; k < transferCount; ++k) {
                    int maxIdx = k;
                    for (int j = k + 1; j < unstableCount; ++j) {
                        if (topSlopes[j] > topSlopes[maxIdx]) {
                            maxIdx = j;
                        }
                    }
                    // Swap to position k
                    if (maxIdx != k) {
                        std::swap(topIndices[k], topIndices[maxIdx]);
                        std::swap(topSlopes[k], topSlopes[maxIdx]);
                        std::swap(topHeightDiffs[k], topHeightDiffs[maxIdx]);
                    }
                }

                // Calculate total excess for top neighbors only
                float totalExcess = 0.0f;
                for (int i = 0; i < transferCount; ++i) {
                    totalExcess += topSlopes[i] - talusAngle;
                }

                // Distribute material to steepest neighbors only
                for (int i = 0; i < transferCount; ++i) {
                    const float excessSlope = topSlopes[i] - talusAngle;
                    const float proportion = excessSlope / totalExcess;
                    const float transfer = topHeightDiffs[i] * 0.5f * proportion * erosionRate;

                    localDelta[idx] -= transfer;
                    localDelta[topIndices[i]] += transfer;
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

    // Storage for top N steepest neighbors
    size_t topIndices[8];
    float topSlopes[8];
    float topHeightDiffs[8];

    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            const size_t idx = y * w + x;
            const float centerHeight = heightData[idx];

            int unstableCount = 0;

            // Single pass: collect unstable neighbors
            for (int i = neighborOffset; i < neighborCount; ++i) {
                const int nx = static_cast<int>(x) + dx[i];
                const int ny = static_cast<int>(y) + dy[i];
                const size_t nidx = ny * w + nx;
                const float neighborHeight = heightData[nidx];
                const float heightDiff = centerHeight - neighborHeight;
                const float slope = heightDiff / dist[i];

                if (slope > talusAngle) {
                    topIndices[unstableCount] = nidx;
                    topSlopes[unstableCount] = slope;
                    topHeightDiffs[unstableCount] = heightDiff;
                    ++unstableCount;
                }
            }

            if (unstableCount == 0) continue;
            ++changes;

            // Find top N steepest using partial selection
            int transferCount = std::min(unstableCount, maxTransferNeighbors);

            for (int k = 0; k < transferCount; ++k) {
                int maxIdx = k;
                for (int j = k + 1; j < unstableCount; ++j) {
                    if (topSlopes[j] > topSlopes[maxIdx]) {
                        maxIdx = j;
                    }
                }
                if (maxIdx != k) {
                    std::swap(topIndices[k], topIndices[maxIdx]);
                    std::swap(topSlopes[k], topSlopes[maxIdx]);
                    std::swap(topHeightDiffs[k], topHeightDiffs[maxIdx]);
                }
            }

            // Calculate total excess for top neighbors only
            float totalExcess = 0.0f;
            for (int i = 0; i < transferCount; ++i) {
                totalExcess += topSlopes[i] - talusAngle;
            }

            // Distribute material to steepest neighbors only
            for (int i = 0; i < transferCount; ++i) {
                const float excessSlope = topSlopes[i] - talusAngle;
                const float proportion = excessSlope / totalExcess;
                const float transfer = topHeightDiffs[i] * 0.5f * proportion * erosionRate;

                m_deltaBuffer[idx] -= transfer;
                m_deltaBuffer[topIndices[i]] += transfer;
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
