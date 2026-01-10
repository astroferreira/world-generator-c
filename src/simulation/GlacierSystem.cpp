#include "simulation/GlacierSystem.hpp"
#include <algorithm>
#include <cmath>

#ifdef WORLDGEN_USE_OPENMP
#include <omp.h>
#endif

namespace worldgen {

GlacierSystem::GlacierSystem() = default;

GlacierSystem::GlacierSystem(const GlacierParams& params)
    : m_params(params)
{
}

void GlacierSystem::initialize(TerrainData& terrain) {
    m_terrain = &terrain;
    m_iterations = 0;

    if (!terrain.hydrology) {
        terrain.hydrology = std::make_unique<HydrologyState>(
            HydrologyState::create(terrain.width(), terrain.heightDim())
        );
    }

    terrain.hydrology->iceThickness->fill(0.0f);
    terrain.hydrology->snowpack->fill(0.0f);
    terrain.hydrology->meltwater->fill(0.0f);

#ifdef WORLDGEN_USE_OPENMP
    // Pre-allocate thread-local buffers
    const size_t totalSize = terrain.width() * terrain.heightDim();
    int maxThreads = omp_get_max_threads();
    m_threadLocalIceFlow.resize(maxThreads);
    for (auto& buffer : m_threadLocalIceFlow) {
        buffer.resize(totalSize, 0.0f);
    }
#endif
}

void GlacierSystem::step() {
    if (!m_terrain || !m_terrain->hydrology) return;

    accumulateSnow();
    compactSnowToIce();
    calculateMelt();

    if (m_params.enableGlacierFlow) {
        flowGlaciers();
        erodeUnderGlaciers();
    }

    contributeMeltwater();

    ++m_iterations;
}

void GlacierSystem::reset() {
    m_iterations = 0;
}

bool GlacierSystem::isComplete() const {
    return m_iterations >= m_params.maxIterations;
}

void GlacierSystem::setParams(const GlacierParams& params) {
    m_params = params;
}

//------------------------------------------------------------------------------
// Snow Accumulation
//------------------------------------------------------------------------------

void GlacierSystem::accumulateSnow() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& snowpack = *m_terrain->hydrology->snowpack;
    Season season = m_terrain->hydrology->currentSeason;

    const float seasonMult = (season == Season::Winter) ? m_params.winterSnowMultiplier : 1.0f;
    const float accumRate = m_params.snowAccumulationRate;
    const float snowlineElev = m_params.snowlineElevation;
    const float maxSnow = m_params.maxSnowDepth;
    const float* heightData = m_terrain->height->data();
    float* snowData = snowpack.data();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t i = 0; i < w * h; ++i) {
        float elev = heightData[i];
        if (elev >= snowlineElev) {
            float accum = accumRate * seasonMult;
            float elevBonus = (elev - snowlineElev) / (1.0f - snowlineElev + 0.001f);
            accum *= (1.0f + elevBonus);

            float newSnow = snowData[i] + accum;
            snowData[i] = std::min(newSnow, maxSnow);
        }
    }
}

//------------------------------------------------------------------------------
// Snow to Ice Compaction
//------------------------------------------------------------------------------

void GlacierSystem::compactSnowToIce() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& snowpack = *m_terrain->hydrology->snowpack;
    auto& ice = *m_terrain->hydrology->iceThickness;

    const float glacierElev = m_params.glacierElevation;
    const float maxSnow = m_params.maxSnowDepth;
    const float compactRate = m_params.iceCompactionRate;
    const float maxIce = m_params.maxIceThickness;
    const float threshold = maxSnow * 0.5f;
    const float* heightData = m_terrain->height->data();
    float* snowData = snowpack.data();
    float* iceData = ice.data();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t i = 0; i < w * h; ++i) {
        float elev = heightData[i];
        if (elev >= glacierElev) {
            float snow = snowData[i];
            if (snow > threshold) {
                float conversion = snow * compactRate;
                snowData[i] -= conversion;

                float newIce = iceData[i] + conversion;
                iceData[i] = std::min(newIce, maxIce);
            }
        }
    }
}

//------------------------------------------------------------------------------
// Melting
//------------------------------------------------------------------------------

void GlacierSystem::calculateMelt() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& snowpack = *m_terrain->hydrology->snowpack;
    auto& ice = *m_terrain->hydrology->iceThickness;
    auto& meltwater = *m_terrain->hydrology->meltwater;

    Season season = m_terrain->hydrology->currentSeason;
    const float seasonMult = (season == Season::Summer) ? m_params.summerMeltMultiplier : 1.0f;
    const float meltRate = m_params.meltRatePerDegree;
    const float snowlineElev = m_params.snowlineElevation;
    const float sublimRate = m_params.sublimationRate;
    const float* heightData = m_terrain->height->data();
    float* snowData = snowpack.data();
    float* iceData = ice.data();
    float* meltData = meltwater.data();

    // Clear meltwater
    meltwater.fill(0.0f);

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t i = 0; i < w * h; ++i) {
        float elev = heightData[i];
        float temp = (snowlineElev - elev) * 10.0f;

        // Apply seasonal temperature adjustment
        switch (season) {
            case Season::Winter: temp -= 5.0f; break;
            case Season::Summer: temp += 5.0f; break;
            default: break;
        }

        if (temp > 0.0f) {
            // Melt snow first
            float snow = snowData[i];
            float snowMelt = std::min(snow, temp * meltRate * seasonMult);
            snowData[i] -= snowMelt;
            meltData[i] += snowMelt;

            // Then melt ice below snowline
            float iceVal = iceData[i];
            if (iceVal > 0.0f && elev < snowlineElev) {
                float iceMelt = std::min(iceVal, temp * meltRate * 0.5f * seasonMult);
                iceData[i] -= iceMelt;
                meltData[i] += iceMelt;
            }
        }

        // Sublimation
        float iceVal = iceData[i];
        if (iceVal > 0.0f) {
            iceData[i] = std::max(0.0f, iceVal - sublimRate);
        }
    }
}

float GlacierSystem::getTemperature(size_t x, size_t y) const {
    float elev = m_terrain->height->get(x, y);
    float temp = (m_params.snowlineElevation - elev) * 10.0f;

    Season season = m_terrain->hydrology->currentSeason;
    switch (season) {
        case Season::Winter: temp -= 5.0f; break;
        case Season::Spring: temp += 0.0f; break;
        case Season::Summer: temp += 5.0f; break;
        case Season::Autumn: temp += 0.0f; break;
    }

    return temp;
}

//------------------------------------------------------------------------------
// Glacier Flow - OPTIMIZED with thread-local buffers
//------------------------------------------------------------------------------

void GlacierSystem::flowGlaciers() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();
    const size_t totalSize = w * h;

    auto& ice = *m_terrain->hydrology->iceThickness;
    const float flowVisc = m_params.flowViscosity;
    const float maxIce = m_params.maxIceThickness;
    float* iceData = ice.data();

#ifdef WORLDGEN_USE_OPENMP
    const float* heightData = m_terrain->height->data();
    // Clear thread-local buffers
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        std::fill(m_threadLocalIceFlow[tid].begin(), m_threadLocalIceFlow[tid].end(), 0.0f);
    }

    // Main flow calculation with thread-local accumulation (no atomics)
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        float* localFlow = m_threadLocalIceFlow[tid].data();

        #pragma omp for schedule(static)
        for (size_t y = 1; y < h - 1; ++y) {
            for (size_t x = 1; x < w - 1; ++x) {
                size_t idx = y * w + x;
                float iceVal = iceData[idx];
                if (iceVal < 0.01f) continue;

                // Calculate flow direction from surface gradient
                float ice_xm = iceData[idx - 1];
                float ice_xp = iceData[idx + 1];
                float ice_ym = iceData[idx - w];
                float ice_yp = iceData[idx + w];

                float surf_xm = heightData[idx - 1] + ice_xm;
                float surf_xp = heightData[idx + 1] + ice_xp;
                float surf_ym = heightData[idx - w] + ice_ym;
                float surf_yp = heightData[idx + w] + ice_yp;

                float gradX = (surf_xm - surf_xp) * 0.5f;
                float gradY = (surf_ym - surf_yp) * 0.5f;

                float flowMag = std::sqrt(gradX * gradX + gradY * gradY);
                if (flowMag < 0.001f) continue;

                float flowRate = iceVal * flowMag * flowVisc;

                // Determine flow direction
                int dx = (gradX > 0.01f) ? -1 : ((gradX < -0.01f) ? 1 : 0);
                int dy = (gradY > 0.01f) ? -1 : ((gradY < -0.01f) ? 1 : 0);

                if (dx != 0 || dy != 0) {
                    int nx = static_cast<int>(x) + dx;
                    int ny = static_cast<int>(y) + dy;

                    if (nx >= 0 && nx < static_cast<int>(w) &&
                        ny >= 0 && ny < static_cast<int>(h)) {
                        localFlow[idx] -= flowRate;
                        localFlow[ny * w + nx] += flowRate;
                    }
                }
            }
        }
    }

    // Reduce all thread-local buffers and apply to ice
    const int numThreads = static_cast<int>(m_threadLocalIceFlow.size());
    #pragma omp parallel for schedule(static)
    for (size_t i = 0; i < totalSize; ++i) {
        float sum = 0.0f;
        for (int t = 0; t < numThreads; ++t) {
            sum += m_threadLocalIceFlow[t][i];
        }
        float newVal = iceData[i] + sum;
        iceData[i] = std::max(0.0f, std::min(newVal, maxIce));
    }

#else
    // Sequential version with single buffer
    std::vector<float> iceFlow(totalSize, 0.0f);

    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            size_t idx = y * w + x;
            float iceVal = iceData[idx];
            if (iceVal < 0.01f) continue;

            Vec2f flowDir = getGlacierFlowDirection(x, y);
            float flowMag = std::sqrt(flowDir.x * flowDir.x + flowDir.y * flowDir.y);

            if (flowMag < 0.001f) continue;

            float flowRate = iceVal * flowMag * flowVisc;

            int nx = static_cast<int>(x) + (flowDir.x > 0.01f ? 1 : (flowDir.x < -0.01f ? -1 : 0));
            int ny = static_cast<int>(y) + (flowDir.y > 0.01f ? 1 : (flowDir.y < -0.01f ? -1 : 0));

            if (nx >= 0 && nx < static_cast<int>(w) &&
                ny >= 0 && ny < static_cast<int>(h)) {
                iceFlow[idx] -= flowRate;
                iceFlow[ny * w + nx] += flowRate;
            }
        }
    }

    for (size_t i = 0; i < totalSize; ++i) {
        float newVal = iceData[i] + iceFlow[i];
        iceData[i] = std::max(0.0f, std::min(newVal, maxIce));
    }
#endif
}

Vec2f GlacierSystem::getGlacierFlowDirection(size_t x, size_t y) const {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    Vec2f gradient = {0.0f, 0.0f};

    if (x > 0 && x < w - 1) {
        float ice_xm = m_terrain->hydrology->iceThickness->get(x - 1, y);
        float ice_xp = m_terrain->hydrology->iceThickness->get(x + 1, y);
        float surf_xm = m_terrain->height->get(x - 1, y) + ice_xm;
        float surf_xp = m_terrain->height->get(x + 1, y) + ice_xp;
        gradient.x = (surf_xm - surf_xp) * 0.5f;
    }

    if (y > 0 && y < h - 1) {
        float ice_ym = m_terrain->hydrology->iceThickness->get(x, y - 1);
        float ice_yp = m_terrain->hydrology->iceThickness->get(x, y + 1);
        float surf_ym = m_terrain->height->get(x, y - 1) + ice_ym;
        float surf_yp = m_terrain->height->get(x, y + 1) + ice_yp;
        gradient.y = (surf_ym - surf_yp) * 0.5f;
    }

    return gradient;
}

//------------------------------------------------------------------------------
// Glacial Erosion
//------------------------------------------------------------------------------

void GlacierSystem::erodeUnderGlaciers() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    const auto& ice = *m_terrain->hydrology->iceThickness;
    const float erosionStr = m_params.erosionStrength;
    const float* iceData = ice.data();
    float* heightData = m_terrain->height->data();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            size_t idx = y * w + x;
            float iceVal = iceData[idx];

            if (iceVal > 0.05f) {
                float erosion = iceVal * erosionStr;
                heightData[idx] -= erosion;
            }
        }
    }
}

//------------------------------------------------------------------------------
// Meltwater Contribution - Enhanced with downstream tracing for visible streams
//------------------------------------------------------------------------------

void GlacierSystem::contributeMeltwater() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    const auto& meltwater = *m_terrain->hydrology->meltwater;
    const float* meltData = meltwater.data();
    const float meltFlowMultiplier = 15.0f;  // Strong contribution to flow

    // Add meltwater to flow accumulation
    if (m_terrain->hydrology->flowAccumulation) {
        float* flowData = m_terrain->hydrology->flowAccumulation->data();
        #ifdef WORLDGEN_USE_OPENMP
        #pragma omp parallel for schedule(static)
        #endif
        for (size_t i = 0; i < w * h; ++i) {
            float melt = meltData[i];
            if (melt > 0.0f) {
                flowData[i] += melt * meltFlowMultiplier;
            }
        }
    }

    // Add to water for visibility at source
    float* waterData = m_terrain->water->data();
    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t i = 0; i < w * h; ++i) {
        float melt = meltData[i];
        if (melt > 0.001f) {
            waterData[i] += melt * 0.5f;
        }
    }

    // Trace meltwater downstream to create visible meltwater streams
    // This ensures meltwater is visible as it flows down from glaciers/snowpack
    if (!m_terrain->hydrology->flowDirection || !m_terrain->hydrology->riverChannel) {
        return;
    }

    const float* flowDirData = m_terrain->hydrology->flowDirection->data();
    float* channelData = m_terrain->hydrology->riverChannel->data();
    const float* heightData = m_terrain->height->data();

    // Direction offsets (D8)
    const int dx[8] = { 0,  1, 1, 1, 0, -1, -1, -1};
    const int dy[8] = {-1, -1, 0, 1, 1,  1,  0, -1};

    // Find significant meltwater sources and trace them downstream
    // Process sequentially to avoid race conditions on channel accumulation
    std::vector<std::pair<size_t, float>> meltSources;
    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            size_t idx = y * w + x;
            float melt = meltData[idx];
            if (melt > 0.002f) {  // Significant melt threshold
                meltSources.push_back({idx, melt});
            }
        }
    }

    // Sort by elevation (highest first) for proper downstream propagation
    std::sort(meltSources.begin(), meltSources.end(),
              [heightData](const std::pair<size_t, float>& a, const std::pair<size_t, float>& b) {
                  return heightData[a.first] > heightData[b.first];
              });

    // Trace each meltwater source downstream
    for (const auto& source : meltSources) {
        size_t idx = source.first;
        float meltAmount = source.second;

        // Trace downstream, accumulating visibility
        const int maxSteps = 200;  // Limit trace length
        float accumulatedMelt = meltAmount;

        for (int step = 0; step < maxSteps; ++step) {
            size_t x = idx % w;
            size_t y = idx / w;

            // Add to channel visibility based on accumulated melt
            float channelDepth = accumulatedMelt * 0.02f;  // Scale for visibility
            channelData[idx] = std::max(channelData[idx], channelDepth);

            // Add to water for direct visibility
            waterData[idx] += accumulatedMelt * 0.1f;

            // Get flow direction
            uint8_t flowDir = static_cast<uint8_t>(flowDirData[idx]);
            if (flowDir == 0) break;  // No flow direction (pit or boundary)

            // Find the direction bit
            int dirIdx = -1;
            for (int d = 0; d < 8; ++d) {
                if (flowDir & (1 << d)) {
                    dirIdx = d;
                    break;
                }
            }
            if (dirIdx < 0) break;

            // Move to next cell
            int nx = static_cast<int>(x) + dx[dirIdx];
            int ny = static_cast<int>(y) + dy[dirIdx];

            if (nx < 1 || nx >= static_cast<int>(w) - 1 ||
                ny < 1 || ny >= static_cast<int>(h) - 1) {
                break;  // Reached boundary
            }

            size_t nextIdx = ny * w + nx;

            // Check if we're below snowline (stop tracing when we merge with regular rivers)
            if (heightData[nextIdx] < m_params.snowlineElevation * 0.8f) {
                break;  // Merged with lower-elevation hydrology
            }

            // Accumulate any additional meltwater at this cell
            accumulatedMelt += meltData[nextIdx] * 0.5f;

            // Decay slightly as we flow downstream
            accumulatedMelt *= 0.98f;

            if (accumulatedMelt < 0.0001f) break;  // Faded out

            idx = nextIdx;
        }
    }
}

//------------------------------------------------------------------------------
// Accessors
//------------------------------------------------------------------------------

float GlacierSystem::getIceThickness(size_t x, size_t y) const {
    if (!m_terrain || !m_terrain->hydrology) return 0.0f;
    return m_terrain->hydrology->iceThickness->get(x, y);
}

float GlacierSystem::getSnowDepth(size_t x, size_t y) const {
    if (!m_terrain || !m_terrain->hydrology) return 0.0f;
    return m_terrain->hydrology->snowpack->get(x, y);
}

bool GlacierSystem::isGlaciated(size_t x, size_t y) const {
    return getIceThickness(x, y) > 0.01f;
}

bool GlacierSystem::hasSnow(size_t x, size_t y) const {
    return getSnowDepth(x, y) > 0.005f;
}

bool GlacierSystem::isAboveSnowline(size_t x, size_t y) const {
    if (!m_terrain) return false;
    return m_terrain->height->get(x, y) >= m_params.snowlineElevation;
}

} // namespace worldgen
