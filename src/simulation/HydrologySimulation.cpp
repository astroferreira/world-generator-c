#include "simulation/HydrologySimulation.hpp"
#include "utils/Profiler.hpp"
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
    PROFILE_SCOPE("HydrologySimulation::step");
    if (!m_terrain || !m_terrain->hydrology) return;

    if (!m_initialized) {
        // Progressive initialization - do one phase per frame to avoid blocking
        switch (m_initPhase) {
            case HydrologyInitPhase::NotStarted:
                m_terrain->water->fill(0.0f);
                m_initPhase = HydrologyInitPhase::FillDepressions;
                return;  // Exit early, continue next frame

            case HydrologyInitPhase::FillDepressions:
                fillDepressions();
                m_initPhase = HydrologyInitPhase::FlowDirections;
                return;

            case HydrologyInitPhase::FlowDirections:
                calculateFlowDirections();
                m_initPhase = HydrologyInitPhase::MoistureFactor;
                return;

            case HydrologyInitPhase::MoistureFactor:
                calculateMoistureFactor();
                m_initPhase = HydrologyInitPhase::FlowAccumulation;
                return;

            case HydrologyInitPhase::FlowAccumulation:
                calculateFlowAccumulation();
                m_initPhase = HydrologyInitPhase::StreamOrders;
                return;

            case HydrologyInitPhase::StreamOrders:
                calculateStreamOrders();
                m_initPhase = HydrologyInitPhase::StreamPower;
                return;

            case HydrologyInitPhase::StreamPower:
                calculateStreamPower();
                m_initPhase = HydrologyInitPhase::Rainfall;
                return;

            case HydrologyInitPhase::Rainfall:
                addRainfall();
                m_initPhase = HydrologyInitPhase::Springs;
                return;

            case HydrologyInitPhase::Springs:
                addSpringFlow();
                m_initPhase = HydrologyInitPhase::SpringChannels;
                return;

            case HydrologyInitPhase::SpringChannels:
                createSpringChannels();
                m_initPhase = HydrologyInitPhase::Channels;
                return;

            case HydrologyInitPhase::Channels:
                initializeChannelsFromFlow();
                m_initPhase = HydrologyInitPhase::WaterParticles;
                return;

            case HydrologyInitPhase::WaterParticles:
                initializeWaterParticles();
                m_initPhase = HydrologyInitPhase::Rivulets;
                return;

            case HydrologyInitPhase::Rivulets:
                calculateRivulets();
                m_initPhase = HydrologyInitPhase::Lakes;
                return;

            case HydrologyInitPhase::Lakes:
                formLakes();
                m_initPhase = HydrologyInitPhase::Rivers;
                return;

            case HydrologyInitPhase::Rivers:
                buildRiverNetwork();
                m_initPhase = HydrologyInitPhase::Complete;
                m_initialized = true;
                // Fall through to normal processing
                break;

            case HydrologyInitPhase::Complete:
                m_initialized = true;
                break;
        }
    }

    if (m_initialized) {
        // Periodically recalculate the full flow network to respond to terrain changes
        bool shouldRecalculate = (m_iterations % m_params.recalculateInterval) == 0;

        if (shouldRecalculate) {
            // Full recalculation - terrain may have changed from erosion
            recalculateFlowNetwork();
        } else {
            // Lighter update - dynamics only
            updateWaterParticles();   // Age water, apply evaporation and erosion loss

            // Stream-power erosion (valley carving) - every other step
            if ((m_iterations % 2) == 0) {
                erodeWithStreamPower();
            }

            // Capacity-based sediment transport
            transportSediment();

            // Legacy erosion (still useful for detailed channel shaping)
            erodeRiverChannels();
            formDeltas();
            applySedimentToTerrain(); // Gradually apply sediment to terrain
            updateSeasonalFlow();
            buildRiverNetwork();
        }
    }

    ++m_iterations;
}

void HydrologySimulation::recalculateFlowNetwork() {
    PROFILE_SCOPE("HydrologySimulation::recalculateFlowNetwork");
    if (!m_terrain || !m_terrain->hydrology) return;

    // Clear existing water to rebuild from scratch
    m_terrain->water->fill(0.0f);

    // Recalculate everything based on current terrain
    fillDepressions();
    calculateFlowDirections();
    calculateMoistureFactor();  // Rain shadow effect
    addRainfall();
    addSpringFlow();
    calculateFlowAccumulation();

    // Stream hierarchy calculations (must be after flow accumulation)
    calculateStreamOrders();
    calculateStreamPower();

    // Initialize river channels based on flow accumulation
    // This provides immediate channel structure for water to appear in
    initializeChannelsFromFlow();
    createSpringChannels();

    // Water particle system - initialize water from sources
    initializeWaterParticles();

    // Fine detail streams from rainfall
    calculateRivulets();

    // Stream-power erosion for valley carving
    erodeWithStreamPower();

    // Capacity-based sediment transport
    transportSediment();

    formLakes();
    buildRiverNetwork();
}

void HydrologySimulation::reset() {
    m_iterations = 0;
    m_lastChange = 1.0f;
    m_initialized = false;
    m_initPhase = HydrologyInitPhase::NotStarted;
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
    PROFILE_SCOPE("HydrologySimulation::fillDepressions");
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
    PROFILE_SCOPE("HydrologySimulation::calculateFlowDirections");
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
    PROFILE_SCOPE("HydrologySimulation::calculateFlowAccumulation");
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
    const auto& moisture = *m_terrain->hydrology->moistureFactor;

    float seasonMult = getSeasonalMultiplier();
    const float baseRainfall = m_params.baseRainfall;
    const float* moistureData = moisture.data();
    float* flowData = flowAcc.data();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t i = 0; i < w * h; ++i) {
        // Moisture factor includes orographic enhancement and rain shadow
        float rainfall = baseRainfall * moistureData[i];
        flowData[i] = rainfall * seasonMult;
    }
}

void HydrologySimulation::addSpringFlow() {
    // Initialize springs if not done yet
    if (!m_terrain->hydrology->springsInitialized) {
        initializeSprings();
    }

    auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    float seasonMult = getSeasonalMultiplier();

    // Add flow from all persistent springs
    for (const auto& spring : m_terrain->hydrology->springs) {
        flowAcc.add(spring.x, spring.y, spring.flowRate * seasonMult);
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
// Water particle system - water has lifetime and is lost through evaporation/erosion
//------------------------------------------------------------------------------

void HydrologySimulation::buildRiverNetwork() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& water = *m_terrain->water;
    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    const auto& riverChannel = *m_terrain->hydrology->riverChannel;
    const auto& lakeDepth = *m_terrain->hydrology->lakeDepth;
    const auto& waterAmount = *m_terrain->hydrology->waterAmount;

    float maxFlow = flowAcc.max();
    float seasonMult = getSeasonalMultiplier();
    float riverThreshold = maxFlow * m_params.riverThresholdRatio;

    const float* heightData = m_terrain->height->data();
    const float* flowData = flowAcc.data();
    const float* channelData = riverChannel.data();
    const float* lakeData = lakeDepth.data();
    const float* waterAmountData = waterAmount.data();
    float* waterData = water.data();
    const float seaLevel = m_params.seaLevel;

    // Minimum channel depth required to show water (prevents water on slopes)
    const float minChannelDepth = 0.0005f;  // Reduced for earlier visibility

    // Water only appears where it can physically stay:
    // 1. In carved river channels (riverChannel > minChannelDepth)
    // 2. In depressions/lakes (lakeDepth > 0)
    // 3. At local minima where water pools
    // 4. Where water particles exist (waterAmount > 0)

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
            float waterAmt = waterAmountData[idx];

            // Case 1: Lake/depression - water pools here
            if (lake > 0.0f) {
                waterData[idx] = std::min(lake, m_params.maxLakeDepth) * seasonMult;
                continue;
            }

            // Case 2: River channel with flow and water particles
            // Water appears if there's a channel AND flow AND water particles present
            if (channel > minChannelDepth && flow > riverThreshold) {
                // Water depth based on:
                // 1. Channel capacity
                // 2. Flow accumulation (surface runoff)
                // 3. Water particle amount (sustained by sources, lost to evap/erosion)
                float flowFactor = std::min(1.0f, flow / (maxFlow * 0.1f));

                // Water particles provide the actual water volume
                // More particles = more visible water
                float waterContribution = waterAmt * 2.0f;

                float waterDepth = channel * (flowFactor + waterContribution) * seasonMult;

                // Cap at channel depth but ensure minimum visibility for rivers
                waterDepth = std::min(waterDepth, channel * 1.5f);
                waterDepth = std::max(waterDepth, channel * 0.3f);  // Minimum 30% of channel

                waterData[idx] = waterDepth;
                continue;
            }

            // Case 2b: Water particles alone can show water in channels
            // Even without significant flow, accumulated water particles are visible
            if (channel > minChannelDepth && waterAmt > 0.01f) {
                float waterDepth = std::min(waterAmt * 2.0f, channel) * seasonMult;
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
    const auto& flowDir = *m_terrain->hydrology->flowDirection;
    const auto& water = *m_terrain->water;

    float maxFlow = flowAcc.max();
    float riverThreshold = maxFlow * 0.02f;
    const float deltaRate = m_params.deltaFormationRate;
    const float maxLakeDepth = m_params.maxLakeDepth;

    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            float flow = flowAcc.get(x, y);
            if (flow < riverThreshold) continue;

            // Get flow direction to find downstream
            uint8_t dir = static_cast<uint8_t>(flowDir.get(x, y));
            if (dir == 0) continue;

            // Find downstream direction that enters water
            int waterDx = 0, waterDy = 0;
            bool entersDeepWater = false;

            for (int i = 0; i < 8; ++i) {
                if ((dir & (1 << i)) == 0) continue;  // Only check flow direction

                int nx = static_cast<int>(x) + DIRECTION_OFFSETS[i].dx;
                int ny = static_cast<int>(y) + DIRECTION_OFFSETS[i].dy;

                if (nx >= 0 && nx < static_cast<int>(w) &&
                    ny >= 0 && ny < static_cast<int>(h)) {
                    if (water.get(nx, ny) > maxLakeDepth * 0.5f) {
                        entersDeepWater = true;
                        waterDx = DIRECTION_OFFSETS[i].dx;
                        waterDy = DIRECTION_OFFSETS[i].dy;
                        break;
                    }
                }
            }

            if (entersDeepWater) {
                float deposit = (flow / maxFlow) * deltaRate;

                // Concentrated deposition at river mouth (70%)
                m_terrain->sediment->add(x, y, deposit * 0.7f);

                // Small fan in flow direction only (20%)
                int fanX = static_cast<int>(x) + waterDx;
                int fanY = static_cast<int>(y) + waterDy;
                if (fanX >= 0 && fanX < static_cast<int>(w) &&
                    fanY >= 0 && fanY < static_cast<int>(h)) {
                    m_terrain->sediment->add(fanX, fanY, deposit * 0.2f);
                }

                // Minimal perpendicular spread (5% each side)
                int perpX1 = static_cast<int>(x) - waterDy;
                int perpY1 = static_cast<int>(y) + waterDx;
                int perpX2 = static_cast<int>(x) + waterDy;
                int perpY2 = static_cast<int>(y) - waterDx;

                if (perpX1 >= 0 && perpX1 < static_cast<int>(w) &&
                    perpY1 >= 0 && perpY1 < static_cast<int>(h)) {
                    m_terrain->sediment->add(perpX1, perpY1, deposit * 0.05f);
                }
                if (perpX2 >= 0 && perpX2 < static_cast<int>(w) &&
                    perpY2 >= 0 && perpY2 < static_cast<int>(h)) {
                    m_terrain->sediment->add(perpX2, perpY2, deposit * 0.05f);
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
// Water Particle System
// Water has lifetime and is lost through evaporation and erosion.
// Sources (rain, springs, melt) add water; sinks (evap, erosion) remove it.
//------------------------------------------------------------------------------

void HydrologySimulation::initializeWaterParticles() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& waterAmount = *m_terrain->hydrology->waterAmount;
    auto& waterAge = *m_terrain->hydrology->waterAge;
    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    const auto& riverChannel = *m_terrain->hydrology->riverChannel;

    float* waterAmtData = waterAmount.data();
    float* ageData = waterAge.data();
    const float* flowData = flowAcc.data();
    const float* channelData = riverChannel.data();

    float maxFlow = flowAcc.max();
    if (maxFlow < 0.001f) maxFlow = 0.001f;

    float seasonMult = getSeasonalMultiplier();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            size_t idx = y * w + x;

            float flow = flowData[idx];
            float channel = channelData[idx];

            // Initialize water amount based on flow accumulation
            // More flow = more water particles
            float normalizedFlow = flow / maxFlow;
            float waterAmt = normalizedFlow * seasonMult;

            // Channels hold more water
            if (channel > 0.0f) {
                waterAmt += channel * 0.5f;
            }

            waterAmtData[idx] = waterAmt;
            ageData[idx] = 0.0f;  // Fresh water
        }
    }

    // Add water from springs
    for (const auto& spring : m_terrain->hydrology->springs) {
        size_t idx = spring.y * w + spring.x;
        waterAmtData[idx] += spring.flowRate * seasonMult;
    }
}

void HydrologySimulation::updateWaterParticles() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& waterAmount = *m_terrain->hydrology->waterAmount;
    auto& waterAge = *m_terrain->hydrology->waterAge;
    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    const auto& riverChannel = *m_terrain->hydrology->riverChannel;
    const auto& streamPower = *m_terrain->hydrology->streamPower;

    float* waterAmtData = waterAmount.data();
    float* ageData = waterAge.data();
    const float* flowData = flowAcc.data();
    const float* channelData = riverChannel.data();
    const float* powerData = streamPower.data();
    const float* heightData = m_terrain->height->data();

    const float evapRate = m_params.evaporationRate;
    const float erosionLoss = m_params.erosionWaterLoss;
    const float tempBonus = m_params.temperatureEvapBonus;
    const float lifetime = m_params.waterLifetimeBase;
    const float seasonMult = getSeasonalMultiplier();
    const float seaLevel = m_params.seaLevel;

    float maxFlow = flowAcc.max();
    if (maxFlow < 0.001f) maxFlow = 0.001f;

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            size_t idx = y * w + x;

            float waterAmt = waterAmtData[idx];
            float age = ageData[idx];
            float elev = heightData[idx];

            if (waterAmt <= 0.0f || elev < seaLevel) {
                waterAmtData[idx] = 0.0f;
                ageData[idx] = 0.0f;
                continue;
            }

            // 1. Age the water
            age += 1.0f;

            // 2. Evaporation - higher at lower elevations (warmer)
            float elevFactor = 1.0f - elev;  // Lower = warmer = more evap
            float evaporation = evapRate * (1.0f + elevFactor * tempBonus);

            // Summer = more evaporation
            if (m_terrain->hydrology->currentSeason == Season::Summer) {
                evaporation *= 1.5f;
            } else if (m_terrain->hydrology->currentSeason == Season::Winter) {
                evaporation *= 0.3f;
            }

            waterAmt -= evaporation;

            // 3. Erosion consumes water (energy transfer)
            float power = powerData[idx];
            float erosionConsumption = power * erosionLoss;
            waterAmt -= erosionConsumption;

            // 4. Lifetime decay - old water "disappears" (absorbed, seeped away)
            if (age > lifetime) {
                float decayFactor = (age - lifetime) / lifetime;
                waterAmt -= waterAmt * decayFactor * 0.1f;
            }

            // 5. Sources add new water
            // Add from flow (rain contribution)
            float flow = flowData[idx];
            float newWater = (flow / maxFlow) * seasonMult * 0.1f;
            waterAmt += newWater;

            // Reset age for fresh water contribution
            if (newWater > waterAmt * 0.5f) {
                age = age * 0.5f;  // Rejuvenate with fresh water
            }

            // 6. Channels retain water better
            float channel = channelData[idx];
            if (channel > 0.001f) {
                // Channels reduce evaporation (deeper water)
                waterAmt += channel * 0.02f;
            }

            // Clamp
            waterAmtData[idx] = std::max(0.0f, waterAmt);
            ageData[idx] = age;
        }
    }

    // Springs continuously add fresh water
    for (const auto& spring : m_terrain->hydrology->springs) {
        size_t idx = spring.y * w + spring.x;
        waterAmtData[idx] += spring.flowRate * seasonMult * 0.5f;
        ageData[idx] = 0.0f;  // Spring water is fresh
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
    const float minChannel = 0.001f;  // Minimum channel depth for visibility

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

            // Ensure minimum channel depth
            channelDepth = std::max(channelDepth, minChannel);

            // Only increase channel, don't decrease (erosion is cumulative)
            if (channelDepth > channelData[i]) {
                channelData[i] = channelDepth;
            }
        }
    }
}

//------------------------------------------------------------------------------
// Spring Network - Terrain-aware persistent spring placement
//------------------------------------------------------------------------------

float HydrologySimulation::calculateTerrainCurvature(size_t x, size_t y) const {
    // Laplacian curvature: positive = concave (valley), negative = convex (ridge)
    const float center = m_terrain->height->get(x, y);
    float sum = 0.0f;
    int count = 0;

    // Check 4 cardinal neighbors
    if (x > 0) { sum += m_terrain->height->get(x - 1, y); count++; }
    if (x < m_terrain->width() - 1) { sum += m_terrain->height->get(x + 1, y); count++; }
    if (y > 0) { sum += m_terrain->height->get(x, y - 1); count++; }
    if (y < m_terrain->heightDim() - 1) { sum += m_terrain->height->get(x, y + 1); count++; }

    if (count == 0) return 0.0f;
    return (sum / count) - center;  // Positive = valley (concave)
}

float HydrologySimulation::calculateSpringQuality(size_t x, size_t y) const {
    float maxFlow = m_terrain->hydrology->flowAccumulation->max();
    return calculateSpringQualityFast(x, y, maxFlow);
}

float HydrologySimulation::calculateSpringQualityFast(size_t x, size_t y, float maxFlow) const {
    float elev = m_terrain->height->get(x, y);

    // 1. Elevation band score (prefer mid-elevations where springs emerge)
    float elevScore = 0.0f;
    if (elev >= m_params.springElevationMin && elev <= m_params.springElevationMax) {
        float midpoint = (m_params.springElevationMin + m_params.springElevationMax) / 2.0f;
        float range = (m_params.springElevationMax - m_params.springElevationMin) / 2.0f;
        elevScore = 1.0f - std::abs(elev - midpoint) / range;
    }

    // 2. Curvature score (prefer valleys where groundwater surfaces)
    float curvature = calculateTerrainCurvature(x, y);
    float curvatureScore = std::max(0.0f, curvature * 10.0f);
    curvatureScore = std::min(1.0f, curvatureScore);

    // 3. Flow accumulation score (springs emerge where water converges)
    float flowAcc = m_terrain->hydrology->flowAccumulation->get(x, y);
    float flowScore = (maxFlow > 0.001f) ? std::sqrt(flowAcc / maxFlow) : 0.0f;

    // 4. Slope score (prefer gentle slopes where water can pool)
    Vec2f gradient = m_terrain->height->getGradient(x, y);
    float slope = std::sqrt(gradient.x * gradient.x + gradient.y * gradient.y);
    float slopeScore = std::exp(-slope * 5.0f);

    // Weighted combination
    return m_params.springElevationWeight * elevScore +
           m_params.springCurvatureWeight * curvatureScore +
           m_params.springFlowAccumWeight * flowScore +
           m_params.springSlopeWeight * slopeScore;
}

void HydrologySimulation::calculateSpringLocations() {
    PROFILE_SCOPE("HydrologySimulation::calculateSpringLocations");
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();
    auto& state = *m_terrain->hydrology;

    // Pre-cache the max flow accumulation (avoid O(n) scan per cell!)
    const float maxFlow = m_terrain->hydrology->flowAccumulation->max();

    // Collect candidate springs with quality scores - parallelized
    std::vector<std::tuple<float, size_t, size_t>> candidates;

    {
        PROFILE_SCOPE("  springs::candidateCollection");
        const float elevMin = m_params.springElevationMin;
        const float elevMax = m_params.springElevationMax;

        // Subsample terrain - since springs are spaced at least springMinSpacing apart,
        // we don't need to check every cell. Use stride = spacing/2 for good coverage.
        const size_t stride = std::max(static_cast<size_t>(1),
                                       static_cast<size_t>(m_params.springMinSpacing / 2.0f));

        #ifdef WORLDGEN_USE_OPENMP
        // Thread-local candidate vectors
        std::vector<std::vector<std::tuple<float, size_t, size_t>>> threadCandidates(omp_get_max_threads());

        // Create list of y values to process (for OpenMP parallelization)
        std::vector<size_t> yValues;
        for (size_t y = 1; y < h - 1; y += stride) {
            yValues.push_back(y);
        }

        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            auto& localCandidates = threadCandidates[tid];

            #pragma omp for schedule(static)
            for (size_t i = 0; i < yValues.size(); ++i) {
                size_t y = yValues[i];
                for (size_t x = 1; x < w - 1; x += stride) {
                    // Early elevation filtering - skip cells outside valid band
                    float elev = m_terrain->height->get(x, y);
                    if (elev < elevMin || elev > elevMax) continue;

                    float quality = calculateSpringQualityFast(x, y, maxFlow);
                    if (quality > 0.3f) {
                        localCandidates.emplace_back(quality, x, y);
                    }
                }
            }
        }

        // Merge thread-local results
        for (const auto& tc : threadCandidates) {
            candidates.insert(candidates.end(), tc.begin(), tc.end());
        }
        #else
        for (size_t y = 1; y < h - 1; y += stride) {
            for (size_t x = 1; x < w - 1; x += stride) {
                // Early elevation filtering
                float elev = m_terrain->height->get(x, y);
                if (elev < elevMin || elev > elevMax) continue;

                float quality = calculateSpringQualityFast(x, y, maxFlow);
                if (quality > 0.3f) {
                    candidates.emplace_back(quality, x, y);
                }
            }
        }
        #endif
    }

    {
        PROFILE_SCOPE("  springs::partialSort");
        // Use partial sort - we only need top maxSprings candidates
        // This is O(n) instead of O(n log n) for full sort
        size_t numToSort = std::min(static_cast<size_t>(m_params.maxSprings * 3), candidates.size());
        if (numToSort < candidates.size()) {
            std::partial_sort(candidates.begin(), candidates.begin() + numToSort,
                              candidates.end(), std::greater<>());
            candidates.resize(numToSort);
        } else {
            std::sort(candidates.begin(), candidates.end(), std::greater<>());
        }
    }

    {
        PROFILE_SCOPE("  springs::spacingCheck");
        // Select top springs with minimum spacing using spatial grid for O(1) lookups
        state.springs.clear();
        const float minSpacing = m_params.springMinSpacing;
        const float minSpacingSq = minSpacing * minSpacing;

        // Initialize spatial grid for fast neighbor lookup
        const int gridWidth = static_cast<int>(std::ceil(static_cast<float>(w) / minSpacing));
        const int gridHeight = static_cast<int>(std::ceil(static_cast<float>(h) / minSpacing));
        std::vector<std::vector<size_t>> spatialGrid(gridWidth * gridHeight);

        auto cellIndex = [gridWidth, minSpacing](float px, float py) -> int {
            int cx = static_cast<int>(px / minSpacing);
            int cy = static_cast<int>(py / minSpacing);
            return cy * gridWidth + cx;
        };

        auto hasNearbySpring = [&](float px, float py) -> bool {
            int cx = static_cast<int>(px / minSpacing);
            int cy = static_cast<int>(py / minSpacing);

            // Check 3x3 neighborhood
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = cx + dx;
                    int ny = cy + dy;
                    if (nx < 0 || nx >= gridWidth || ny < 0 || ny >= gridHeight) continue;

                    for (size_t idx : spatialGrid[ny * gridWidth + nx]) {
                        float ddx = px - static_cast<float>(state.springs[idx].x);
                        float ddy = py - static_cast<float>(state.springs[idx].y);
                        if (ddx * ddx + ddy * ddy < minSpacingSq) return true;
                    }
                }
            }
            return false;
        };

        for (const auto& [quality, x, y] : candidates) {
            if (static_cast<int>(state.springs.size()) >= m_params.maxSprings) break;

            float px = static_cast<float>(x);
            float py = static_cast<float>(y);

            // O(1) spatial lookup instead of O(k) linear scan
            if (hasNearbySpring(px, py)) continue;

            SpringLocation spring;
            spring.x = x;
            spring.y = y;
            spring.flowRate = m_params.springFlowRate * quality;
            spring.quality = quality;

            size_t springIdx = state.springs.size();
            state.springs.push_back(spring);
            spatialGrid[cellIndex(px, py)].push_back(springIdx);
        }
    }
}

void HydrologySimulation::initializeSprings() {
    auto& state = *m_terrain->hydrology;
    if (state.springsInitialized) return;

    calculateSpringLocations();
    state.springsInitialized = true;
}

//------------------------------------------------------------------------------
// Rain Shadow Effect
//------------------------------------------------------------------------------

float HydrologySimulation::sampleUpwindElevation(size_t x, size_t y, float distance,
                                                   float upwindDx, float upwindDy) const {
    // Sample along upwind direction - reduced samples for performance
    float maxElev = 0.0f;
    const int samples = static_cast<int>(distance / 15.0f);  // Sample every 15 pixels (was 5)
    if (samples <= 0) return 0.0f;

    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    for (int i = 1; i <= samples; ++i) {
        float sampleDist = distance * i / samples;
        float sx = static_cast<float>(x) + upwindDx * sampleDist;
        float sy = static_cast<float>(y) + upwindDy * sampleDist;

        // Bounds check with integer conversion for faster get()
        int ix = static_cast<int>(sx + 0.5f);
        int iy = static_cast<int>(sy + 0.5f);
        if (ix < 0 || static_cast<size_t>(ix) >= w ||
            iy < 0 || static_cast<size_t>(iy) >= h) {
            continue;
        }

        // Use nearest-neighbor lookup instead of interpolated (much faster)
        float elev = m_terrain->height->get(static_cast<size_t>(ix), static_cast<size_t>(iy));
        maxElev = std::max(maxElev, elev);
    }

    return maxElev;
}

void HydrologySimulation::calculateMoistureFactor() {
    PROFILE_SCOPE("HydrologySimulation::calculateMoistureFactor");
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();
    auto& moisture = *m_terrain->hydrology->moistureFactor;

    const float shadowStrength = m_params.rainShadowStrength;
    const float shadowDistance = m_params.rainShadowDistance;
    const float depletionRate = m_params.moistureDepletionRate;
    const float elevBonus = m_params.elevationRainfallBonus;

    // Pre-cache wind direction vector (computed once, not per-cell)
    const float windRad = m_params.windDirection * 3.14159265f / 180.0f;
    const float upwindDx = std::cos(windRad);
    const float upwindDy = std::sin(windRad);

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            float localElev = m_terrain->height->get(x, y);
            float upwindElev = sampleUpwindElevation(x, y, shadowDistance, upwindDx, upwindDy);

            // Start with base moisture factor of 1.0
            float factor = 1.0f;

            if (upwindElev > localElev) {
                // We're in a rain shadow - upwind terrain is higher
                // More moisture was already released on the windward side
                float elevDiff = upwindElev - localElev;
                float shadow = elevDiff * depletionRate;
                factor = std::max(1.0f - shadowStrength, 1.0f - shadow);
            } else {
                // We're on the windward slope or flat - enhanced precipitation
                // Orographic lift causes moisture to condense
                factor = 1.0f + localElev * elevBonus;
            }

            moisture.set(x, y, std::clamp(factor, 0.1f, 2.0f));
        }
    }
}

//------------------------------------------------------------------------------
// Sediment Application
//------------------------------------------------------------------------------

void HydrologySimulation::applySedimentToTerrain() {
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    const float applicationRate = 0.1f;  // Apply 10% of sediment per step
    float* heightData = m_terrain->height->data();
    float* sedimentData = m_terrain->sediment->data();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t i = 0; i < w * h; ++i) {
        float sediment = sedimentData[i];
        if (sediment > 0.0001f) {
            float transfer = sediment * applicationRate;
            heightData[i] += transfer;
            sedimentData[i] -= transfer;
        }
    }
}

//------------------------------------------------------------------------------
// Stream Order (Strahler) - Realistic Drainage Hierarchy
//------------------------------------------------------------------------------

void HydrologySimulation::calculateStreamOrders() {
    PROFILE_SCOPE("HydrologySimulation::calculateStreamOrders");
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& streamOrder = *m_terrain->hydrology->streamOrder;
    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    const auto& flowDir = *m_terrain->hydrology->flowDirection;

    // Initialize all to 0 (no stream)
    streamOrder.fill(0.0f);

    float maxFlow = flowAcc.max();
    float threshold = maxFlow * m_params.firstOrderThreshold;

    // Sort cells by elevation (highest first) for proper order propagation
    std::vector<std::pair<float, size_t>> cells;
    cells.reserve(w * h);
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            float flow = flowAcc.get(x, y);
            if (flow >= threshold) {
                cells.emplace_back(getEffectiveElevation(x, y), y * w + x);
            }
        }
    }
    std::sort(cells.begin(), cells.end(), std::greater<>());

    // Track incoming orders at each cell
    std::vector<std::vector<int>> incomingOrders(w * h);

    // First pass: collect incoming orders from upstream
    for (const auto& [elev, idx] : cells) {
        size_t x = idx % w;
        size_t y = idx / w;

        uint8_t dir = static_cast<uint8_t>(flowDir.get(x, y));
        if (dir == 0) continue;

        // Get current order (default to 1 for headwaters)
        int currentOrder = static_cast<int>(streamOrder.get(x, y));
        if (currentOrder == 0) {
            // No upstream tributaries - this is order 1 (headwater)
            currentOrder = 1;
            streamOrder.set(x, y, 1.0f);
        }

        // Find downstream cell and record our order there
        for (int i = 0; i < 8; ++i) {
            if ((dir & (1 << i)) == 0) continue;

            int nx = static_cast<int>(x) + DIRECTION_OFFSETS[i].dx;
            int ny = static_cast<int>(y) + DIRECTION_OFFSETS[i].dy;

            if (nx >= 0 && nx < static_cast<int>(w) &&
                ny >= 0 && ny < static_cast<int>(h)) {
                incomingOrders[ny * w + nx].push_back(currentOrder);
            }
            break;  // Only one downstream direction
        }
    }

    // Second pass: calculate Strahler orders from incoming
    for (const auto& [elev, idx] : cells) {
        const auto& incoming = incomingOrders[idx];
        if (incoming.empty()) continue;

        int maxOrder = *std::max_element(incoming.begin(), incoming.end());
        int countMax = static_cast<int>(std::count(incoming.begin(), incoming.end(), maxOrder));

        // Strahler rule: if two streams of same order merge, result is order+1
        int newOrder = (countMax >= 2) ? maxOrder + 1 : maxOrder;
        newOrder = std::min(newOrder, m_params.maxStreamOrder);

        streamOrder.set(idx % w, idx / w, static_cast<float>(newOrder));
    }
}

//------------------------------------------------------------------------------
// Stream Power - For Erosion Calculation
//------------------------------------------------------------------------------

void HydrologySimulation::calculateStreamPower() {
    PROFILE_SCOPE("HydrologySimulation::calculateStreamPower");
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& streamPower = *m_terrain->hydrology->streamPower;
    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    const auto& streamOrder = *m_terrain->hydrology->streamOrder;
    const float* heightData = m_terrain->height->data();

    float maxFlow = flowAcc.max();

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            size_t idx = y * w + x;

            float flow = flowAcc.get(x, y);
            int order = static_cast<int>(streamOrder.get(x, y));

            if (order == 0) {
                streamPower.set(x, y, 0.0f);
                continue;
            }

            // Calculate local slope (maximum gradient)
            float centerH = heightData[idx];
            float maxSlope = 0.0f;

            for (int d = 0; d < 8; ++d) {
                int nx = static_cast<int>(x) + DIRECTION_OFFSETS[d].dx;
                int ny = static_cast<int>(y) + DIRECTION_OFFSETS[d].dy;
                float dh = centerH - heightData[ny * w + nx];
                float slope = dh / DIRECTION_OFFSETS[d].distance;
                maxSlope = std::max(maxSlope, slope);
            }

            // Stream Power Index: A^0.5 * S
            // (where A = drainage area proxy from flow, S = slope)
            float normalizedFlow = flow / maxFlow;
            float spi = std::sqrt(normalizedFlow) * maxSlope;

            // Scale by stream order (higher order = more erosive power)
            spi *= (1.0f + order * 0.5f);

            streamPower.set(x, y, spi);
        }
    }
}

//------------------------------------------------------------------------------
// Rivulets - Fine Detail Streams from Rainfall
//------------------------------------------------------------------------------

void HydrologySimulation::calculateRivulets() {
    PROFILE_SCOPE("HydrologySimulation::calculateRivulets");
    if (!m_params.showRivulets) return;

    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& water = *m_terrain->water;
    const auto& flowAcc = *m_terrain->hydrology->flowAccumulation;
    const auto& streamOrder = *m_terrain->hydrology->streamOrder;
    const float* heightData = m_terrain->height->data();

    float maxFlow = flowAcc.max();
    float rivuletThreshold = maxFlow * m_params.rivuletThreshold;
    float seasonMult = getSeasonalMultiplier();
    const float seaLevel = m_params.seaLevel;

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 1; y < h - 1; ++y) {
        for (size_t x = 1; x < w - 1; ++x) {
            size_t idx = y * w + x;

            if (heightData[idx] < seaLevel) continue;

            float flow = flowAcc.get(x, y);
            int order = static_cast<int>(streamOrder.get(x, y));

            if (flow > rivuletThreshold) {
                // Calculate local concavity (valley detection)
                float centerH = heightData[idx];
                float avgNeighbor = 0.0f;
                for (int d = 0; d < 8; ++d) {
                    int nx = static_cast<int>(x) + DIRECTION_OFFSETS[d].dx;
                    int ny = static_cast<int>(y) + DIRECTION_OFFSETS[d].dy;
                    avgNeighbor += heightData[ny * w + nx];
                }
                avgNeighbor /= 8.0f;
                float concavity = std::max(0.0f, avgNeighbor - centerH);

                // Water depth based on stream order and flow
                float baseDepth = 0.0f;
                if (order >= 1) {
                    // Scale by order using width scale array
                    int orderIdx = std::min(order, 7);
                    baseDepth = m_params.orderWidthScale[orderIdx] * m_params.rivuletDepthScale;
                } else {
                    // Sub-order-1 rivulet (tiny)
                    baseDepth = m_params.rivuletDepthScale * 0.5f * (flow / rivuletThreshold);
                }

                // Enhance depth in concave areas (valleys)
                float valleyBonus = concavity * 5.0f;
                float waterDepth = (baseDepth + valleyBonus) * seasonMult;

                // Only show if there's a significant valley or high flow
                if (concavity > 0.0005f || flow > rivuletThreshold * 5.0f) {
                    water.set(x, y, std::max(water.get(x, y), waterDepth));
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
// Spring Channels - Visible Streams from Spring Sources
//------------------------------------------------------------------------------

void HydrologySimulation::createSpringChannels() {
    PROFILE_SCOPE("HydrologySimulation::createSpringChannels");
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    auto& riverChannel = *m_terrain->hydrology->riverChannel;
    auto& water = *m_terrain->water;
    const auto& flowDir = *m_terrain->hydrology->flowDirection;
    const auto& streamOrder = *m_terrain->hydrology->streamOrder;

    for (const auto& spring : m_terrain->hydrology->springs) {
        // Mark spring source as water
        water.add(spring.x, spring.y, spring.flowRate * m_params.springWaterVisibility);

        // Trace downstream and create visible channel
        size_t x = spring.x;
        size_t y = spring.y;
        float remainingFlow = spring.flowRate;
        int maxSteps = 150;  // Limit to prevent infinite loops

        for (int step = 0; step < maxSteps && remainingFlow > 0.001f; ++step) {
            // Check if we've merged with a larger stream
            int order = static_cast<int>(streamOrder.get(x, y));
            if (order >= 2) break;  // Merged with significant stream

            // Add to channel depth
            riverChannel.add(x, y, remainingFlow * m_params.springChannelDepth);

            // Add visible water
            water.add(x, y, remainingFlow * m_params.springWaterVisibility * 0.5f);

            // Follow flow direction
            uint8_t dir = static_cast<uint8_t>(flowDir.get(x, y));
            if (dir == 0) break;

            bool foundNext = false;
            for (int i = 0; i < 8; ++i) {
                if ((dir & (1 << i)) == 0) continue;

                int nx = static_cast<int>(x) + DIRECTION_OFFSETS[i].dx;
                int ny = static_cast<int>(y) + DIRECTION_OFFSETS[i].dy;

                if (nx >= 0 && nx < static_cast<int>(w) &&
                    ny >= 0 && ny < static_cast<int>(h)) {
                    x = static_cast<size_t>(nx);
                    y = static_cast<size_t>(ny);
                    foundNext = true;
                    break;
                }
            }

            if (!foundNext) break;
            remainingFlow *= 0.97f;  // Gradual decrease
        }
    }
}

//------------------------------------------------------------------------------
// Stream-Power Erosion - Valley Carving
//------------------------------------------------------------------------------

void HydrologySimulation::erodeWithStreamPower() {
    PROFILE_SCOPE("HydrologySimulation::erodeWithStreamPower");
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    const auto& streamPower = *m_terrain->hydrology->streamPower;
    const auto& streamOrder = *m_terrain->hydrology->streamOrder;
    auto& riverChannel = *m_terrain->hydrology->riverChannel;

    float* heightData = m_terrain->height->data();
    float* channelData = riverChannel.data();

    const float erosionCoeff = m_params.streamPowerErosionCoeff;
    const float maxErosion = m_params.maxChannelErosionPerStep;
    const float lateralFactor = m_params.lateralErosionFactor;

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 2; y < h - 2; ++y) {
        for (size_t x = 2; x < w - 2; ++x) {
            size_t idx = y * w + x;

            float spi = streamPower.get(x, y);
            int order = static_cast<int>(streamOrder.get(x, y));

            if (spi < 0.0001f) continue;

            // Erosion rate proportional to stream power
            float erosion = spi * erosionCoeff;
            erosion = std::min(erosion, maxErosion);

            // V-shaped valley: erode center
            heightData[idx] -= erosion;
            channelData[idx] += erosion;

            // Lateral erosion for valley widening (scales with order)
            if (order >= 2) {
                float lateralErosion = erosion * lateralFactor * (static_cast<float>(order) / 7.0f);
                for (int d = 0; d < 8; ++d) {
                    int nx = static_cast<int>(x) + DIRECTION_OFFSETS[d].dx;
                    int ny = static_cast<int>(y) + DIRECTION_OFFSETS[d].dy;
                    size_t nidx = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                    heightData[nidx] -= lateralErosion;
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
// Improved Sediment Transport - Capacity-Based
//------------------------------------------------------------------------------

void HydrologySimulation::transportSediment() {
    PROFILE_SCOPE("HydrologySimulation::transportSediment");
    const size_t w = m_terrain->width();
    const size_t h = m_terrain->heightDim();

    const auto& flowDir = *m_terrain->hydrology->flowDirection;
    const auto& streamPower = *m_terrain->hydrology->streamPower;
    const auto& streamOrder = *m_terrain->hydrology->streamOrder;

    // Sort stream cells by elevation for proper downstream transport
    std::vector<std::pair<float, size_t>> cells;
    cells.reserve(w * h / 10);  // Estimate ~10% are streams

    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            if (streamOrder.get(x, y) > 0) {
                cells.emplace_back(getEffectiveElevation(x, y), y * w + x);
            }
        }
    }
    std::sort(cells.begin(), cells.end(), std::greater<>());

    // Sediment load tracking (transported downstream)
    std::vector<float> sedimentLoad(w * h, 0.0f);

    float* heightData = m_terrain->height->data();
    float* sedimentData = m_terrain->sediment->data();

    for (const auto& [elev, idx] : cells) {
        size_t x = idx % w;
        size_t y = idx / w;

        float spi = streamPower.get(x, y);
        int order = static_cast<int>(streamOrder.get(x, y));

        // Transport capacity based on stream power and order
        float capacity = spi * m_params.sedimentCapacityCoeff * (1.0f + order * 0.5f);

        float currentLoad = sedimentLoad[idx];

        if (currentLoad < capacity && spi > 0.001f) {
            // Can carry more - erode
            float erosion = (capacity - currentLoad) * m_params.sedimentErodeRate;
            erosion = std::min(erosion, 0.001f);  // Cap per-step erosion
            heightData[idx] -= erosion;
            sedimentLoad[idx] += erosion;
        } else if (currentLoad > capacity) {
            // Overloaded - deposit
            float deposit = (currentLoad - capacity) * m_params.sedimentDepositRate;
            sedimentData[idx] += deposit;
            sedimentLoad[idx] -= deposit;
        }

        // Transport remaining sediment downstream
        uint8_t dir = static_cast<uint8_t>(flowDir.get(x, y));
        if (dir == 0) continue;

        for (int i = 0; i < 8; ++i) {
            if ((dir & (1 << i)) == 0) continue;

            int nx = static_cast<int>(x) + DIRECTION_OFFSETS[i].dx;
            int ny = static_cast<int>(y) + DIRECTION_OFFSETS[i].dy;

            if (nx >= 0 && nx < static_cast<int>(w) &&
                ny >= 0 && ny < static_cast<int>(h)) {
                sedimentLoad[static_cast<size_t>(ny) * w + static_cast<size_t>(nx)] += sedimentLoad[idx];
            }
            break;  // Only one downstream direction
        }
    }
}

} // namespace worldgen
