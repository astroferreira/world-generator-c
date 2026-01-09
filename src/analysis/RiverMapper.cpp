#include "analysis/RiverMapper.hpp"
#include <algorithm>
#include <cmath>
#include <queue>

#ifdef WORLDGEN_USE_OPENMP
#include <omp.h>
#endif

namespace worldgen {

// D8 direction offsets (N, NE, E, SE, S, SW, W, NW)
static const int DX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int DY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
static const float DIST[8] = {1.0f, 1.414f, 1.0f, 1.414f, 1.0f, 1.414f, 1.0f, 1.414f};

void RiverMapper::calculateFlowDirections(const Heightmap& terrain,
                                           std::vector<int>& flowDir) {
    const size_t w = terrain.width();
    const size_t h = terrain.height();
    const float* data = terrain.data();

    flowDir.resize(w * h, -1);

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            size_t idx = y * w + x;
            float centerHeight = data[idx];

            float maxSlope = 0.0f;
            int bestDir = -1;

            for (int d = 0; d < 8; ++d) {
                int nx = static_cast<int>(x) + DX[d];
                int ny = static_cast<int>(y) + DY[d];

                float neighborHeight = data[ny * w + nx];
                float slope = (centerHeight - neighborHeight) / DIST[d];

                if (slope > maxSlope) {
                    maxSlope = slope;
                    bestDir = d;
                }
            }

            flowDir[idx] = bestDir;
        }
    }
}

void RiverMapper::calculateFlowAccumulation(const Heightmap& terrain,
                                             const std::vector<int>& flowDir,
                                             Heightmap& flowAcc) {
    const size_t w = terrain.width();
    const size_t h = terrain.height();
    const float* heightData = terrain.data();

    // Initialize flow accumulation to 1 (each cell contributes itself)
    flowAcc.fill(1.0f);
    float* accData = flowAcc.data();

    // Sort cells by elevation (highest first)
    std::vector<std::pair<float, size_t>> cells;
    cells.reserve(w * h);

    for (size_t i = 0; i < w * h; ++i) {
        cells.emplace_back(heightData[i], i);
    }

    std::sort(cells.begin(), cells.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    // Process from highest to lowest, accumulating flow
    for (const auto& cell : cells) {
        size_t idx = cell.second;
        int dir = flowDir[idx];

        if (dir >= 0) {
            size_t x = idx % w;
            size_t y = idx / w;
            int nx = static_cast<int>(x) + DX[dir];
            int ny = static_cast<int>(y) + DY[dir];

            if (nx >= 0 && nx < static_cast<int>(w) &&
                ny >= 0 && ny < static_cast<int>(h)) {
                size_t nidx = ny * w + nx;
                accData[nidx] += accData[idx];
            }
        }
    }
}

void RiverMapper::calculateValleyDepth(const Heightmap& terrain,
                                        Heightmap& valleyDepth) {
    const size_t w = terrain.width();
    const size_t h = terrain.height();
    const float* data = terrain.data();
    float* valley = valleyDepth.data();

    // Valley depth = average neighbor height - center height
    // Positive = valley, negative = ridge

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            size_t idx = y * w + x;
            float center = data[idx];

            float sum = 0.0f;
            for (int d = 0; d < 8; ++d) {
                int nx = static_cast<int>(x) + DX[d];
                int ny = static_cast<int>(y) + DY[d];
                sum += data[ny * w + nx];
            }

            float avgNeighbor = sum / 8.0f;
            valley[idx] = std::max(0.0f, avgNeighbor - center);
        }
    }
}

void RiverMapper::findDepressions(const Heightmap& terrain,
                                   Heightmap& lakeMap) {
    const size_t w = terrain.width();
    const size_t h = terrain.height();
    const float* data = terrain.data();
    float* lake = lakeMap.data();

    lakeMap.fill(0.0f);

    // Use priority-flood algorithm to find depressions
    // Start from edges and flood inward, marking cells that would be underwater

    // Track which cells have been processed
    std::vector<bool> processed(w * h, false);

    // Track the "filled" height - water surface level at each cell
    std::vector<float> filledHeight(w * h);
    for (size_t i = 0; i < w * h; ++i) {
        filledHeight[i] = data[i];
    }

    // Priority queue: process lowest cells first
    // pair<height, index>
    auto cmp = [](const std::pair<float, size_t>& a, const std::pair<float, size_t>& b) {
        return a.first > b.first;  // Min-heap
    };
    std::priority_queue<std::pair<float, size_t>,
                        std::vector<std::pair<float, size_t>>,
                        decltype(cmp)> pq(cmp);

    // Initialize with boundary cells (edges and ocean cells)
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            size_t idx = y * w + x;

            // Add boundary cells and ocean cells to queue
            bool isBoundary = (x == 0 || x == w - 1 || y == 0 || y == h - 1);
            bool isOcean = data[idx] < m_config.seaLevel;

            if (isBoundary || isOcean) {
                pq.push({data[idx], idx});
                processed[idx] = true;
            }
        }
    }

    // Process cells from lowest to highest
    while (!pq.empty()) {
        auto [height, idx] = pq.top();
        pq.pop();

        size_t x = idx % w;
        size_t y = idx / w;

        // Check all 8 neighbors
        for (int d = 0; d < 8; ++d) {
            int nx = static_cast<int>(x) + DX[d];
            int ny = static_cast<int>(y) + DY[d];

            if (nx < 0 || nx >= static_cast<int>(w) ||
                ny < 0 || ny >= static_cast<int>(h)) {
                continue;
            }

            size_t nidx = ny * w + nx;
            if (processed[nidx]) continue;

            processed[nidx] = true;

            float neighborHeight = data[nidx];

            // If neighbor is lower than current water level, it's in a depression
            if (neighborHeight < filledHeight[idx]) {
                filledHeight[nidx] = filledHeight[idx];
                // Lake depth = water surface - ground
                lake[nidx] = filledHeight[idx] - neighborHeight;
            } else {
                filledHeight[nidx] = neighborHeight;
            }

            pq.push({filledHeight[nidx], nidx});
        }
    }

    // Only keep lakes above sea level (not ocean)
    for (size_t i = 0; i < w * h; ++i) {
        if (data[i] < m_config.seaLevel) {
            lake[i] = 0.0f;
        }
    }
}

void RiverMapper::smoothMap(Heightmap& map, int passes) {
    const size_t w = map.width();
    const size_t h = map.height();

    std::vector<float> temp(w * h);

    for (int pass = 0; pass < passes; ++pass) {
        float* src = map.data();

        #ifdef WORLDGEN_USE_OPENMP
        #pragma omp parallel for schedule(static)
        #endif
        for (size_t y = 1; y < h - 1; ++y) {
            for (size_t x = 1; x < w - 1; ++x) {
                size_t idx = y * w + x;

                float sum = src[idx] * 2.0f;  // Center weighted
                float weight = 2.0f;

                for (int d = 0; d < 8; ++d) {
                    int nx = static_cast<int>(x) + DX[d];
                    int ny = static_cast<int>(y) + DY[d];
                    sum += src[ny * w + nx];
                    weight += 1.0f;
                }

                temp[idx] = sum / weight;
            }
        }

        // Copy back
        std::copy(temp.begin(), temp.end(), map.data());
    }
}

std::unique_ptr<Heightmap> RiverMapper::generateRiverMap(const Heightmap& terrain) {
    const size_t w = terrain.width();
    const size_t h = terrain.height();

    auto riverMap = std::make_unique<Heightmap>(w, h);

    // Step 1: Calculate flow directions
    std::vector<int> flowDir;
    calculateFlowDirections(terrain, flowDir);

    // Step 2: Calculate flow accumulation
    Heightmap flowAcc(w, h);
    calculateFlowAccumulation(terrain, flowDir, flowAcc);

    // Step 3: Calculate valley depth (for channel width)
    Heightmap valleyDepth(w, h);
    calculateValleyDepth(terrain, valleyDepth);

    // Step 4: Combine into river intensity
    float maxFlow = flowAcc.max();
    float maxValley = valleyDepth.max();
    if (maxFlow < 1.0f) maxFlow = 1.0f;
    if (maxValley < 0.001f) maxValley = 0.001f;

    const float* heightData = terrain.data();
    const float* flowData = flowAcc.data();
    const float* valleyData = valleyDepth.data();
    float* riverData = riverMap->data();

    const float threshold = maxFlow * m_config.riverThreshold;
    const float flowPower = m_config.flowPower;
    const float channelWeight = m_config.channelDepthWeight;

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t i = 0; i < w * h; ++i) {
        // Skip ocean
        if (heightData[i] < m_config.seaLevel) {
            riverData[i] = 0.0f;
            continue;
        }

        float flow = flowData[i];
        float valley = valleyData[i];

        if (flow > threshold) {
            // River intensity based on flow (power law) and valley depth
            float flowIntensity = std::pow((flow - threshold) / (maxFlow - threshold), flowPower);
            float valleyIntensity = valley / maxValley;

            // Combine: flow determines presence, valley affects width
            float intensity = flowIntensity * (1.0f - channelWeight) +
                             flowIntensity * valleyIntensity * channelWeight;

            riverData[i] = std::min(1.0f, intensity);
        } else {
            riverData[i] = 0.0f;
        }
    }

    // Step 5: Smooth for natural appearance
    smoothMap(*riverMap, m_config.smoothingPasses);

    return riverMap;
}

std::unique_ptr<Heightmap> RiverMapper::generateLakeMap(const Heightmap& terrain) {
    const size_t w = terrain.width();
    const size_t h = terrain.height();

    auto lakeMap = std::make_unique<Heightmap>(w, h);

    // First find true depressions
    findDepressions(terrain, *lakeMap);

    // Also find "river pools" - flat areas with high flow accumulation
    // These are places where rivers would naturally spread out: deltas, wetlands, floodplains

    std::vector<int> flowDir(w * h);
    Heightmap flowAcc(w, h);
    calculateFlowDirections(terrain, flowDir);
    calculateFlowAccumulation(terrain, flowDir, flowAcc);

    const float* heightData = terrain.data();
    const float* flowData = flowAcc.data();
    float* lakeData = lakeMap->data();

    float maxFlow = flowAcc.max();
    if (maxFlow < 1.0f) maxFlow = 1.0f;

    // High flow threshold for forming pools (top 1% of flow)
    float poolFlowThreshold = maxFlow * 0.01f;

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 2; y < h - 2; ++y) {
        for (size_t x = 2; x < w - 2; ++x) {
            size_t idx = y * w + x;

            if (heightData[idx] < m_config.seaLevel) continue;  // Skip ocean

            float flow = flowData[idx];
            if (flow < poolFlowThreshold) continue;

            // Calculate local gradient (steepness)
            float centerH = heightData[idx];
            float gradient = 0.0f;
            for (int d = 0; d < 8; ++d) {
                int nx = static_cast<int>(x) + DX[d];
                int ny = static_cast<int>(y) + DY[d];
                float dh = std::abs(heightData[ny * w + nx] - centerH) / DIST[d];
                gradient = std::max(gradient, dh);
            }

            // Low gradient + high flow = river pool/wetland
            // Gradient threshold: flatter = more likely to pool
            if (gradient < 0.02f) {
                float flowIntensity = (flow - poolFlowThreshold) / (maxFlow - poolFlowThreshold);
                float flatness = 1.0f - (gradient / 0.02f);

                // Pool intensity based on flow and flatness
                float poolIntensity = flowIntensity * flatness * 0.5f;

                // Add to existing lake data (don't overwrite depressions)
                lakeData[idx] = std::max(lakeData[idx], poolIntensity);
            }
        }
    }

    return lakeMap;
}

std::unique_ptr<Heightmap> RiverMapper::generateWaterMap(const Heightmap& terrain) {
    auto riverMap = generateRiverMap(terrain);
    auto lakeMap = generateLakeMap(terrain);

    const size_t w = terrain.width();
    const size_t h = terrain.height();

    // Combine rivers and lakes
    float* river = riverMap->data();
    const float* lake = lakeMap->data();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t i = 0; i < w * h; ++i) {
        // Lakes override rivers (deeper water)
        if (lake[i] > 0.01f) {
            river[i] = std::min(1.0f, lake[i] * 5.0f + river[i]);
        }
    }

    return riverMap;
}

} // namespace worldgen
