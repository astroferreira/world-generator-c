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
            updateGroundwater();      // Accumulate groundwater from infiltration
            calculateBaseflow();      // Discharge groundwater to channels
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

    // Initialize river channels based on flow accumulation
    // This provides immediate channel structure for water to appear in
    initializeChannelsFromFlow();

    // Groundwater system - sustains rivers between rain events
    updateGroundwater();
    calculateBaseflow();

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
// River Network - Only shows water where it physically stays
// Now includes baseflow contribution from groundwater system
//------------------------------------------------------------------------------

void HydrologySimulation::buildRiverNetwork() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& water = *m_terrain->water;
    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    const auto& riverChannel = *m_terrain->hydrology->riverChannel;
    const auto& lakeDepth = *m_terrain->hydrology->lakeDepth;
    const auto& baseflow = *m_terrain->hydrology->baseflow;

    float maxFlow = flowAcc.max();
    float seasonMult = getSeasonalMultiplier();
    float riverThreshold = maxFlow * m_params.riverThresholdRatio;

    const float* heightData = m_terrain->height->data();
    const float* flowData = flowAcc.data();
    const float* channelData = riverChannel.data();
    const float* lakeData = lakeDepth.data();
    const float* baseflowData = baseflow.data();
    float* waterData = water.data();
    const float seaLevel = m_params.seaLevel;

    // Minimum channel depth required to show water (prevents water on slopes)
    const float minChannelDepth = 0.0005f;  // Reduced for earlier visibility

    // Water only appears where it can physically stay:
    // 1. In carved river channels (riverChannel > minChannelDepth)
    // 2. In depressions/lakes (lakeDepth > 0)
    // 3. At local minima where water pools
    // 4. NEW: Where baseflow from groundwater sustains rivers

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            size_t idx = y * w + x;
            float elevation = heightData[idx];

            // Skip underwater areas
            if (elevation < seaLevel) {
                waterData[idx] = 0.0f;
                continue;
            }

            float flow = flowData[idx];
            float channel = channelData[idx];
            float lake = lakeData[idx];
            float bf = baseflowData[idx];

            // Case 1: Lake/depression - water pools here
            if (lake > 0.0f) {
                waterData[idx] = std::min(lake, m_params.maxLakeDepth) * seasonMult;
                continue;
            }

            // Case 2: River channel with flow or baseflow
            // Water appears if there's a channel AND (surface flow OR groundwater baseflow)
            if (channel > minChannelDepth && flow > riverThreshold) {
                // Water depth based on:
                // 1. Channel capacity
                // 2. Flow accumulation (surface runoff)
                // 3. Baseflow contribution (groundwater discharge)
                float flowFactor = std::min(1.0f, flow / (maxFlow * 0.1f));

                // Baseflow provides sustained water even when surface flow is low
                // This is why rivers don't dry up between rain events
                float baseflowContribution = bf * 5.0f;  // Amplify baseflow for visibility

                float waterDepth = channel * (flowFactor + baseflowContribution) * seasonMult;

                // Cap at channel depth but ensure minimum visibility for rivers
                waterDepth = std::min(waterDepth, channel * 1.5f);
                waterDepth = std::max(waterDepth, channel * 0.3f);  // Minimum 30% of channel

                waterData[idx] = waterDepth;
                continue;
            }

            // Case 2b: Baseflow alone can sustain water in channels
            // Even without significant surface flow, groundwater can maintain rivers
            if (channel > minChannelDepth && bf > 0.001f) {
                float waterDepth = std::min(bf * 3.0f, channel) * seasonMult;
                waterData[idx] = waterDepth;
                continue;
            }

            // Case 3: Check if this is a local minimum (water would pool)
            if (flow > riverThreshold * 5.0f) {  // Reduced threshold for more pooling
                float centerHeight = elevation;
                bool isLocalMin = true;
                float minNeighborHeight = centerHeight;

                // Check 8 neighbors
                for (int dy = -1; dy <= 1 && isLocalMin; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        size_t nidx = (y + dy) * w + (x + dx);
                        float neighborHeight = heightData[nidx];
                        if (neighborHeight < centerHeight) {
                            isLocalMin = false;
                            break;
                        }
                        minNeighborHeight = std::min(minNeighborHeight, neighborHeight);
                    }
                }

                if (isLocalMin) {
                    // Pool depth is difference to lowest neighbor that would overflow
                    float poolDepth = minNeighborHeight - centerHeight;
                    if (poolDepth > 0.0005f) {
                        waterData[idx] = std::min(poolDepth, 0.15f) * seasonMult;
                        continue;
                    }
                }
            }

            // No valid water location - clear any existing water
            waterData[idx] = 0.0f;
        }
    }
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

//------------------------------------------------------------------------------
// Groundwater / Baseflow System
// Based on real hydrology: precipitation infiltrates into soil, stored in
// aquifers, and slowly discharges to maintain river flow (baseflow)
//------------------------------------------------------------------------------

void HydrologySimulation::updateGroundwater() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& groundwater = *m_terrain->hydrology->groundwater;
    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    const float* heightData = m_terrain->height->data();
    float* gwData = groundwater.data();

    const float infiltration = m_params.infiltrationRate;
    const float capacity = m_params.aquiferCapacity;
    const float permeability = m_params.permeability;
    const float seasonMult = getSeasonalMultiplier();

    // Calculate max flow for normalization
    float maxFlow = flowAcc.max();
    if (maxFlow < 0.001f) maxFlow = 0.001f;

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            size_t idx = y * w + x;

            // Infiltration: rainfall that soaks into ground
            // More infiltration in areas with higher flow (more rainfall upstream)
            float localFlow = flowAcc.get(x, y);
            float flowRatio = localFlow / maxFlow;

            // Infiltration proportional to local precipitation/runoff
            // Higher terrain = more rainfall = more infiltration potential
            float elev = heightData[idx];
            float recharge = infiltration * (m_params.baseRainfall + elev * m_params.elevationRainfallBonus);
            recharge *= seasonMult;

            // Add to groundwater storage
            float currentGW = gwData[idx];
            float newGW = currentGW + recharge;

            // Lateral flow: groundwater flows from high to low areas (simplified)
            // This helps accumulate groundwater in valleys
            float centerElev = elev;
            float lateralIn = 0.0f;
            int neighborCount = 0;

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    size_t nidx = (y + dy) * w + (x + dx);
                    float neighborElev = heightData[nidx];
                    float neighborGW = gwData[nidx];

                    // Water flows from higher to lower ground
                    if (neighborElev > centerElev) {
                        float gradient = (neighborElev - centerElev);
                        lateralIn += neighborGW * gradient * permeability * 0.1f;
                    }
                    neighborCount++;
                }
            }

            newGW += lateralIn / neighborCount;

            // Cap at aquifer capacity
            gwData[idx] = std::min(newGW, capacity);
        }
    }
}

void HydrologySimulation::calculateBaseflow() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& groundwater = *m_terrain->hydrology->groundwater;
    auto& baseflow = *m_terrain->hydrology->baseflow;
    auto& riverChannel = *m_terrain->hydrology->riverChannel;
    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    const float* heightData = m_terrain->height->data();

    const float baseflowRate = m_params.baseflowRate;
    const float riverThreshold = flowAcc.max() * m_params.riverThresholdRatio;

    float* gwData = groundwater.data();
    float* bfData = baseflow.data();
    float* channelData = riverChannel.data();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            size_t idx = y * w + x;

            float gw = gwData[idx];
            float flow = flowAcc.get(x, y);
            float channel = channelData[idx];

            // Baseflow discharge: groundwater releases into channels
            // Higher discharge where:
            // 1. There is significant flow accumulation (river locations)
            // 2. Terrain is lower (valleys, where water table intersects surface)
            // 3. Channels have been carved (erosion has exposed aquifer)

            float discharge = 0.0f;

            if (flow > riverThreshold) {
                // River location - groundwater discharges here
                // Discharge rate proportional to groundwater storage and channel depth
                float channelFactor = 1.0f + channel * 10.0f;  // Deeper channels = more exposure
                discharge = gw * baseflowRate * channelFactor;

                // Remove discharged water from aquifer
                gwData[idx] = std::max(0.0f, gw - discharge);
            }

            // Valley bottoms (local minima) also receive baseflow
            float centerElev = heightData[idx];
            bool isValley = true;
            for (int dy = -1; dy <= 1 && isValley; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    size_t nidx = (y + dy) * w + (x + dx);
                    if (heightData[nidx] < centerElev) {
                        isValley = false;
                        break;
                    }
                }
            }

            if (isValley && gw > 0.01f) {
                float valleyDischarge = gw * baseflowRate * 0.5f;
                discharge += valleyDischarge;
                gwData[idx] = std::max(0.0f, gwData[idx] - valleyDischarge);
            }

            bfData[idx] = discharge;
        }
    }
}

void HydrologySimulation::initializeChannelsFromFlow() {
    // Create initial channel depths based on flow accumulation
    // This allows rivers to appear immediately rather than waiting for erosion
    // Based on the hydrological principle that larger catchments = deeper channels

    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& riverChannel = *m_terrain->hydrology->riverChannel;
    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;

    float maxFlow = flowAcc.max();
    if (maxFlow < 0.001f) return;

    const float riverThreshold = maxFlow * m_params.riverThresholdRatio;
    const float channelFactor = m_params.initialChannelFactor;
    const float minChannel = m_params.minBaseflowChannel;

    float* channelData = riverChannel.data();
    const float* flowData = flowAcc.data();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t i = 0; i < w * h; ++i) {
        float flow = flowData[i];

        if (flow > riverThreshold) {
            // Channel depth scales with flow (catchment area)
            // Using power law: depth ~ flow^0.4 (based on hydraulic geometry)
            float normalizedFlow = flow / maxFlow;
            float channelDepth = std::pow(normalizedFlow, 0.4f) * channelFactor;

            // Ensure minimum channel for baseflow
            channelDepth = std::max(channelDepth, minChannel);

            // Only increase channel, don't decrease (erosion is cumulative)
            if (channelDepth > channelData[i]) {
                channelData[i] = channelDepth;
            }
        }
    }
}

} // namespace worldgen
