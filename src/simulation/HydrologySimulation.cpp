#include "simulation/HydrologySimulation.hpp"
#include <algorithm>
#include <cmath>

#ifdef WORLDGEN_USE_OPENMP
#include <omp.h>
#endif

namespace worldgen {

HydrologySimulation::HydrologySimulation() = default;

HydrologySimulation::HydrologySimulation(const HydrologyParams& params)
    : m_params(params)
{
}

void HydrologySimulation::initialize(TerrainData& terrain) {
    m_terrain = &terrain;
    m_iterations = 0;
    m_lastChange = 1.0f;
    m_initialized = false;

    // Ensure hydrology state exists
    if (!terrain.hydrology) {
        terrain.hydrology = std::make_unique<HydrologyState>(
            HydrologyState::create(terrain.width(), terrain.heightDim())
        );
    }

    // Initialize water heightmap
    terrain.water->fill(0.0f);

    // Prepare visited array
    m_visited.resize(terrain.width() * terrain.heightDim(), false);
}

void HydrologySimulation::step() {
    if (!m_terrain || !m_terrain->hydrology) return;

    if (!m_initialized) {
        // First pass: establish flow network
        recalculateFlowNetwork();
        m_initialized = true;
    } else {
        // Periodically recalculate the full flow network to respond to terrain changes
        bool shouldRecalculate = (m_iterations % m_params.recalculateInterval) == 0;

        if (shouldRecalculate) {
            // Full recalculation - terrain may have changed from erosion
            recalculateFlowNetwork();
        } else {
            // Lighter update - dynamics only
            erodeRiverChannels();
            depositSediment();
            formDeltas();
            updateSeasonalFlow();
            buildRiverNetwork();
        }
    }

    ++m_iterations;
}

void HydrologySimulation::recalculateFlowNetwork() {
    if (!m_terrain || !m_terrain->hydrology) return;

    // Clear existing water to rebuild from scratch
    m_terrain->water->fill(0.0f);

    // Recalculate everything based on current terrain
    fillDepressions();
    calculateFlowDirections();
    addRainfall();
    addSpringFlow();
    calculateFlowAccumulation();
    formLakes();
    buildRiverNetwork();
}

void HydrologySimulation::reset() {
    m_iterations = 0;
    m_lastChange = 1.0f;
    m_initialized = false;
    std::fill(m_visited.begin(), m_visited.end(), false);
    while (!m_pitQueue.empty()) m_pitQueue.pop();
}

bool HydrologySimulation::isComplete() const {
    // In continuous mode, complete after initial setup so other simulations can run,
    // but we'll keep stepping via isContinuous()
    if (m_params.continuous) {
        return m_initialized;  // Complete once initialized, but keep running
    }
    return m_iterations >= m_params.maxIterations;
}

void HydrologySimulation::setParams(const HydrologyParams& params) {
    m_params = params;
}

//------------------------------------------------------------------------------
// Depression Filling (Priority-Flood Algorithm)
//------------------------------------------------------------------------------

void HydrologySimulation::fillDepressions() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    std::fill(m_visited.begin(), m_visited.end(), false);
    while (!m_pitQueue.empty()) m_pitQueue.pop();

    auto& lakeDepth = *m_terrain->hydrology->lakeDepth;
    lakeDepth.fill(0.0f);

    // Seed with boundary cells
    for (size_t x = 0; x < w; ++x) {
        m_pitQueue.push({x, 0, m_terrain->totalHeight(x, 0)});
        m_pitQueue.push({x, h-1, m_terrain->totalHeight(x, h-1)});
        m_visited[0 * w + x] = true;
        m_visited[(h-1) * w + x] = true;
    }
    for (size_t y = 1; y < h - 1; ++y) {
        m_pitQueue.push({0, y, m_terrain->totalHeight(0, y)});
        m_pitQueue.push({w-1, y, m_terrain->totalHeight(w-1, y)});
        m_visited[y * w + 0] = true;
        m_visited[y * w + w-1] = true;
    }

    // Process cells in elevation order
    while (!m_pitQueue.empty()) {
        PitCell cell = m_pitQueue.top();
        m_pitQueue.pop();

        for (int i = 0; i < 8; ++i) {
            int nx = static_cast<int>(cell.x) + DIRECTION_OFFSETS[i].dx;
            int ny = static_cast<int>(cell.y) + DIRECTION_OFFSETS[i].dy;

            if (nx < 0 || nx >= static_cast<int>(w) ||
                ny < 0 || ny >= static_cast<int>(h)) {
                continue;
            }

            size_t nIdx = ny * w + nx;
            if (m_visited[nIdx]) continue;
            m_visited[nIdx] = true;

            float neighborElev = m_terrain->totalHeight(nx, ny);

            if (neighborElev < cell.elevation) {
                // Depression - fill with water
                float depth = cell.elevation - neighborElev;
                lakeDepth.set(nx, ny, depth);
                m_pitQueue.push({static_cast<size_t>(nx),
                                static_cast<size_t>(ny),
                                cell.elevation});
            } else {
                m_pitQueue.push({static_cast<size_t>(nx),
                                static_cast<size_t>(ny),
                                neighborElev});
            }
        }
    }
}

//------------------------------------------------------------------------------
// Flow Direction Calculation (D8 Algorithm)
//------------------------------------------------------------------------------

void HydrologySimulation::calculateFlowDirections() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();
    auto& flowDir = *m_terrain->hydrology->flowDirection;

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            FlowDirection dir = findSteepestDescent(x, y);
            flowDir.set(x, y, static_cast<float>(static_cast<uint8_t>(dir)));
        }
    }
}

FlowDirection HydrologySimulation::findSteepestDescent(size_t x, size_t y) const {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();
    float centerElev = getEffectiveElevation(x, y);

    float maxSlope = 0.0f;
    FlowDirection bestDir = FlowDirection::None;

    for (int i = 0; i < 8; ++i) {
        int nx = static_cast<int>(x) + DIRECTION_OFFSETS[i].dx;
        int ny = static_cast<int>(y) + DIRECTION_OFFSETS[i].dy;

        if (nx < 0 || nx >= static_cast<int>(w) ||
            ny < 0 || ny >= static_cast<int>(h)) {
            continue;
        }

        float neighborElev = getEffectiveElevation(nx, ny);
        float drop = centerElev - neighborElev;
        float slope = drop / DIRECTION_OFFSETS[i].distance;

        if (slope > maxSlope) {
            maxSlope = slope;
            bestDir = static_cast<FlowDirection>(1 << i);
        }
    }

    return bestDir;
}

float HydrologySimulation::getEffectiveElevation(size_t x, size_t y) const {
    float baseElev = m_terrain->totalHeight(x, y);
    if (m_terrain->hydrology->lakeDepth) {
        baseElev += m_terrain->hydrology->lakeDepth->get(x, y);
    }
    return baseElev;
}

//------------------------------------------------------------------------------
// Flow Accumulation
//------------------------------------------------------------------------------

void HydrologySimulation::calculateFlowAccumulation() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    const auto& flowDir = *m_terrain->hydrology->flowDirection;

    // Create cell ordering by elevation (highest first)
    std::vector<std::pair<float, size_t>> cells;
    cells.reserve(w * h);

    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            float elev = getEffectiveElevation(x, y);
            cells.emplace_back(elev, y * w + x);
        }
    }

    // Parallel sort for large arrays
    std::sort(cells.begin(), cells.end(), std::greater<>());

    // Propagate flow downhill (sequential due to dependencies)
    for (const auto& [elev, idx] : cells) {
        size_t x = idx % w;
        size_t y = idx / w;

        uint8_t dir = static_cast<uint8_t>(flowDir.get(x, y));
        if (dir == 0) continue;

        float myFlow = flowAcc.get(x, y);

        for (int i = 0; i < 8; ++i) {
            if ((dir & (1 << i)) == 0) continue;

            int nx = static_cast<int>(x) + DIRECTION_OFFSETS[i].dx;
            int ny = static_cast<int>(y) + DIRECTION_OFFSETS[i].dy;

            if (nx >= 0 && nx < static_cast<int>(w) &&
                ny >= 0 && ny < static_cast<int>(h)) {
                flowAcc.add(nx, ny, myFlow);
            }
        }
    }

    // Apply power law in parallel
    float* flowData = flowAcc.data();
    const float exponent = m_params.flowExponent;
    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t i = 0; i < w * h; ++i) {
        flowData[i] = std::pow(flowData[i], exponent);
    }
}

//------------------------------------------------------------------------------
// Rainfall and Springs
//------------------------------------------------------------------------------

void HydrologySimulation::addRainfall() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    float seasonMult = getSeasonalMultiplier();
    const float baseRainfall = m_params.baseRainfall;
    const float elevBonus = m_params.elevationRainfallBonus;
    const float* heightData = m_terrain->height->data();
    float* flowData = flowAcc.data();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t i = 0; i < w * h; ++i) {
        float elev = heightData[i];
        float rainfall = baseRainfall + elev * elevBonus;
        flowData[i] = rainfall * seasonMult;
    }
}

void HydrologySimulation::addSpringFlow() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    float seasonMult = getSeasonalMultiplier();

    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            float elev = m_terrain->height->get(x, y);

            if (elev >= m_params.springElevationMin &&
                elev <= m_params.springElevationMax) {
                if (m_random.nextFloat() < m_params.springDensity) {
                    flowAcc.add(x, y, m_params.springFlowRate * seasonMult);
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
// Lake Formation
//------------------------------------------------------------------------------

void HydrologySimulation::formLakes() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& water = *m_terrain->water;
    const auto& lakeDepth = *m_terrain->hydrology->lakeDepth;
    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;

    float flowThreshold = flowAcc.max() * 0.01f;
    const float maxLakeDepth = m_params.maxLakeDepth;
    const float* lakeData = lakeDepth.data();
    const float* flowData = flowAcc.data();
    float* waterData = water.data();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t i = 0; i < w * h; ++i) {
        float depth = lakeData[i];
        if (depth > 0.0f && flowData[i] > flowThreshold) {
            waterData[i] = std::min(depth, maxLakeDepth);
        } else if (depth > maxLakeDepth * 0.5f) {
            waterData[i] = depth * 0.5f;
        }
    }
}

//------------------------------------------------------------------------------
// River Network - OPTIMIZED
//------------------------------------------------------------------------------

void HydrologySimulation::buildRiverNetwork() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& water = *m_terrain->water;
    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;

    float maxFlow = flowAcc.max();
    float logMaxFlow = std::log(maxFlow + 1.0f);
    float seasonMult = getSeasonalMultiplier();
    float riverThreshold = maxFlow * m_params.riverThresholdRatio;

    const float* heightData = m_terrain->height->data();
    const float* flowData = flowAcc.data();
    float* waterData = water.data();
    const float seaLevel = m_params.seaLevel;
    const float maxRiverWidth = m_params.maxRiverWidth;
    const float baseRiverDepth = m_params.baseRiverDepth;
    const float riverDepthScale = m_params.riverDepthScale;

    // Phase 1: Calculate river properties in parallel (no writes to shared data)
    struct RiverCell {
        size_t x, y;
        float width;
        float depth;
    };
    std::vector<RiverCell> riverCells;

    // First, count river cells to pre-allocate
    #ifdef WORLDGEN_USE_OPENMP
    std::vector<std::vector<RiverCell>> threadLocalRivers(omp_get_max_threads());

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        auto& localRivers = threadLocalRivers[tid];

        #pragma omp for schedule(static)
        for (size_t y = 0; y < h; ++y) {
            for (size_t x = 0; x < w; ++x) {
                size_t idx = y * w + x;
                float elevation = heightData[idx];
                if (elevation < seaLevel) continue;

                float flow = flowData[idx];
                if (flow <= riverThreshold) continue;

                float logFlow = std::log(flow + 1.0f);
                float normalizedFlow = logFlow / logMaxFlow;

                float riverWidth = normalizedFlow * maxRiverWidth;
                float riverDepth = baseRiverDepth + normalizedFlow * riverDepthScale;
                riverDepth *= seasonMult;

                localRivers.push_back({x, y, riverWidth, riverDepth});
            }
        }
    }

    // Merge thread-local results
    for (const auto& localRivers : threadLocalRivers) {
        riverCells.insert(riverCells.end(), localRivers.begin(), localRivers.end());
    }
    #else
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            size_t idx = y * w + x;
            float elevation = heightData[idx];
            if (elevation < seaLevel) continue;

            float flow = flowData[idx];
            if (flow <= riverThreshold) continue;

            float logFlow = std::log(flow + 1.0f);
            float normalizedFlow = logFlow / logMaxFlow;

            float riverWidth = normalizedFlow * maxRiverWidth;
            float riverDepth = baseRiverDepth + normalizedFlow * riverDepthScale;
            riverDepth *= seasonMult;

            riverCells.push_back({x, y, riverWidth, riverDepth});
        }
    }
    #endif

    // Phase 2: Expand rivers with atomic max operations
    // Use thread-local water buffers to avoid contention
    #ifdef WORLDGEN_USE_OPENMP
    const int numThreads = omp_get_max_threads();
    std::vector<std::vector<float>> threadWater(numThreads);
    for (auto& tw : threadWater) {
        tw.resize(w * h, 0.0f);
    }

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        float* localWater = threadWater[tid].data();

        #pragma omp for schedule(dynamic, 256)
        for (size_t i = 0; i < riverCells.size(); ++i) {
            const auto& rc = riverCells[i];
            int radius = static_cast<int>(rc.width);

            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    float dist = std::sqrt(static_cast<float>(dx*dx + dy*dy));
                    if (dist > rc.width) continue;

                    int nx = static_cast<int>(rc.x) + dx;
                    int ny = static_cast<int>(rc.y) + dy;

                    if (nx < 0 || nx >= static_cast<int>(w) ||
                        ny < 0 || ny >= static_cast<int>(h)) continue;

                    size_t nidx = ny * w + nx;
                    if (heightData[nidx] < seaLevel) continue;

                    float falloff = 1.0f - (dist / (rc.width + 1.0f));
                    float localDepth = rc.depth * falloff;

                    if (localDepth > localWater[nidx]) {
                        localWater[nidx] = localDepth;
                    }
                }
            }
        }
    }

    // Reduce thread-local water to main buffer
    #pragma omp parallel for schedule(static)
    for (size_t i = 0; i < w * h; ++i) {
        float maxWater = waterData[i];
        for (int t = 0; t < numThreads; ++t) {
            if (threadWater[t][i] > maxWater) {
                maxWater = threadWater[t][i];
            }
        }
        waterData[i] = maxWater;
    }
    #else
    // Sequential version
    for (const auto& rc : riverCells) {
        int radius = static_cast<int>(rc.width);

        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                float dist = std::sqrt(static_cast<float>(dx*dx + dy*dy));
                if (dist > rc.width) continue;

                int nx = static_cast<int>(rc.x) + dx;
                int ny = static_cast<int>(rc.y) + dy;

                if (nx < 0 || nx >= static_cast<int>(w) ||
                    ny < 0 || ny >= static_cast<int>(h)) continue;

                size_t nidx = ny * w + nx;
                if (heightData[nidx] < seaLevel) continue;

                float falloff = 1.0f - (dist / (rc.width + 1.0f));
                float localDepth = rc.depth * falloff;

                if (localDepth > waterData[nidx]) {
                    waterData[nidx] = localDepth;
                }
            }
        }
    }
    #endif
}

//------------------------------------------------------------------------------
// River Dynamics
//------------------------------------------------------------------------------

void HydrologySimulation::erodeRiverChannels() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    auto& riverChannel = *m_terrain->hydrology->riverChannel;

    float maxFlow = flowAcc.max();
    float riverThreshold = maxFlow * m_params.riverThresholdRatio;
    const float erosionRate = m_params.channelErosionRate;
    const float* flowData = flowAcc.data();
    float* channelData = riverChannel.data();
    float* heightData = m_terrain->height->data();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            size_t idx = y * w + x;
            float flow = flowData[idx];

            if (flow > riverThreshold) {
                float erosion = (flow / maxFlow) * erosionRate;
                channelData[idx] += erosion;
                heightData[idx] -= erosion;
            }
        }
    }
}

void HydrologySimulation::depositSediment() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    const auto& flowDir = *m_terrain->hydrology->flowDirection;
    const float maxFlow = flowAcc.max();
    const float depositRate = m_params.sedimentDepositionRate;

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            float flow = flowAcc.get(x, y);
            uint8_t dir = static_cast<uint8_t>(flowDir.get(x, y));

            for (int i = 0; i < 8; ++i) {
                if ((dir & (1 << i)) == 0) continue;

                int nx = static_cast<int>(x) + DIRECTION_OFFSETS[i].dx;
                int ny = static_cast<int>(y) + DIRECTION_OFFSETS[i].dy;

                if (nx >= 0 && nx < static_cast<int>(w) &&
                    ny >= 0 && ny < static_cast<int>(h)) {

                    float downstreamFlow = flowAcc.get(nx, ny);

                    if (downstreamFlow < flow * 0.8f) {
                        float deposit = (flow - downstreamFlow) * depositRate / (maxFlow + 1.0f);
                        m_terrain->sediment->add(x, y, deposit);
                    }
                }
            }
        }
    }
}

void HydrologySimulation::formDeltas() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    const auto& water = *m_terrain->water;

    float maxFlow = flowAcc.max();
    float riverThreshold = maxFlow * 0.02f;
    const float deltaRate = m_params.deltaFormationRate;
    const float maxLakeDepth = m_params.maxLakeDepth;

    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            float flow = flowAcc.get(x, y);
            if (flow < riverThreshold) continue;

            // Check if entering deep water
            bool entersWater = false;
            for (int i = 0; i < 8; ++i) {
                int nx = static_cast<int>(x) + DIRECTION_OFFSETS[i].dx;
                int ny = static_cast<int>(y) + DIRECTION_OFFSETS[i].dy;

                if (nx >= 0 && nx < static_cast<int>(w) &&
                    ny >= 0 && ny < static_cast<int>(h)) {
                    if (water.get(nx, ny) > maxLakeDepth * 0.5f) {
                        entersWater = true;
                        break;
                    }
                }
            }

            if (entersWater) {
                float deposit = (flow / maxFlow) * deltaRate;
                m_terrain->sediment->add(x, y, deposit);

                for (int i = 0; i < 8; ++i) {
                    int nx = static_cast<int>(x) + DIRECTION_OFFSETS[i].dx;
                    int ny = static_cast<int>(y) + DIRECTION_OFFSETS[i].dy;

                    if (nx >= 0 && nx < static_cast<int>(w) &&
                        ny >= 0 && ny < static_cast<int>(h)) {
                        m_terrain->sediment->add(nx, ny, deposit * 0.3f);
                    }
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
// Seasonal Updates
//------------------------------------------------------------------------------

void HydrologySimulation::updateSeasonalFlow() {
    auto& state = *m_terrain->hydrology;

    state.seasonProgress += 0.25f;

    if (state.seasonProgress >= 1.0f) {
        state.seasonProgress = 0.0f;
        state.currentSeason = static_cast<Season>(
            (static_cast<int>(state.currentSeason) + 1) % 4
        );

        if (state.currentSeason == Season::Spring) {
            state.yearCount++;
        }
    }
}

float HydrologySimulation::getSeasonalMultiplier() const {
    if (!m_terrain->hydrology) return 1.0f;

    switch (m_terrain->hydrology->currentSeason) {
        case Season::Spring: return m_params.springMultiplier;
        case Season::Summer: return m_params.summerMultiplier;
        case Season::Autumn: return m_params.autumnMultiplier;
        case Season::Winter: return m_params.winterMultiplier;
    }
    return 1.0f;
}

Season HydrologySimulation::currentSeason() const {
    return m_terrain && m_terrain->hydrology ?
           m_terrain->hydrology->currentSeason : Season::Spring;
}

void HydrologySimulation::advanceSeason() {
    if (m_terrain && m_terrain->hydrology) {
        auto& state = *m_terrain->hydrology;
        state.currentSeason = static_cast<Season>(
            (static_cast<int>(state.currentSeason) + 1) % 4
        );
    }
}

void HydrologySimulation::setSeason(Season season) {
    if (m_terrain && m_terrain->hydrology) {
        m_terrain->hydrology->currentSeason = season;
    }
}

} // namespace worldgen
