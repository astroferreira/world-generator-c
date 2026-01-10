#pragma once

#include "ISimulation.hpp"
#include "HydrologyData.hpp"
#include "utils/Random.hpp"
#include <vector>
#include <queue>

namespace worldgen {

// Progressive initialization phases to avoid blocking startup
enum class HydrologyInitPhase {
    NotStarted,
    FillDepressions,
    FlowDirections,
    MoistureFactor,
    FlowAccumulation,
    StreamOrders,        // Strahler stream order calculation
    StreamPower,         // Stream power index for erosion
    Rainfall,
    Springs,
    SpringChannels,      // Create visible channels from springs
    Channels,
    WaterParticles,      // Initialize water particle system
    Rivulets,            // Fine detail streams from rainfall
    Lakes,
    Rivers,
    Complete
};

class HydrologySimulation : public ISimulation {
public:
    HydrologySimulation();
    explicit HydrologySimulation(const HydrologyParams& params);

    std::string name() const override { return "Hydrology"; }
    void initialize(TerrainData& terrain) override;
    void step() override;
    void reset() override;
    int iterations() const override { return m_iterations; }
    bool isComplete() const override;
    bool isContinuous() const override { return m_params.continuous; }

    // Recalculate the entire flow network based on current terrain
    void recalculateFlowNetwork();

    void setParams(const HydrologyParams& params);
    const HydrologyParams& params() const { return m_params; }

    Season currentSeason() const;
    void advanceSeason();
    void setSeason(Season season);

private:
    HydrologyParams m_params;
    TerrainData* m_terrain = nullptr;
    Random m_random;
    int m_iterations = 0;
    float m_lastChange = 1.0f;
    bool m_initialized = false;
    HydrologyInitPhase m_initPhase = HydrologyInitPhase::NotStarted;

    // Priority queue for depression filling
    struct PitCell {
        size_t x, y;
        float elevation;
        bool operator>(const PitCell& other) const {
            return elevation > other.elevation;
        }
    };
    std::priority_queue<PitCell, std::vector<PitCell>, std::greater<PitCell>> m_pitQueue;
    std::vector<bool> m_visited;

    // Core algorithms
    void fillDepressions();
    void calculateFlowDirections();
    void calculateFlowAccumulation();
    void addRainfall();
    void addSpringFlow();
    void formLakes();
    void buildRiverNetwork();
    void erodeRiverChannels();
    void depositSediment();
    void formDeltas();
    void updateSeasonalFlow();

    // Water particle system - tracks water lifetime and loss
    void initializeWaterParticles();
    void updateWaterParticles();    // Age water, apply evaporation/erosion loss
    void initializeChannelsFromFlow();

    // Spring network - terrain-aware persistent springs
    void initializeSprings();
    void calculateSpringLocations();
    float calculateSpringQuality(size_t x, size_t y) const;
    float calculateSpringQualityFast(size_t x, size_t y, float maxFlow) const;
    float calculateTerrainCurvature(size_t x, size_t y) const;

    // Rain shadow effect
    void calculateMoistureFactor();
    float sampleUpwindElevation(size_t x, size_t y, float distance,
                                float upwindDx, float upwindDy) const;

    // Stream order and power - for realistic drainage hierarchy
    void calculateStreamOrders();
    void calculateStreamPower();

    // Rivulets - fine detail streams from rainfall
    void calculateRivulets();

    // Spring channels - visible streams from spring sources
    void createSpringChannels();

    // Stream-power erosion - valley carving
    void erodeWithStreamPower();

    // Improved sediment transport
    void transportSediment();

    // Sediment application
    void applySedimentToTerrain();

    // Helpers
    FlowDirection findSteepestDescent(size_t x, size_t y) const;
    float getEffectiveElevation(size_t x, size_t y) const;
    float getSeasonalMultiplier() const;
};

} // namespace worldgen
