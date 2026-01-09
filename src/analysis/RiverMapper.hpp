#pragma once

#include "terrain/Heightmap.hpp"
#include <memory>
#include <vector>

namespace worldgen {

// Analyzes terrain topology to generate river/water maps
// Works on eroded terrain - doesn't simulate particles, just analyzes shape
class RiverMapper {
public:
    struct Config {
        float seaLevel = 0.32f;
        float riverThreshold = 0.001f;    // Flow accumulation threshold for rivers (lower = more rivers)
        float channelDepthWeight = 0.3f;  // How much valley depth affects river width
        float flowPower = 0.4f;           // Power law for flow->width (lower = wider rivers)
        int smoothingPasses = 3;          // Smooth the river map
    };

    RiverMapper() = default;
    explicit RiverMapper(const Config& config) : m_config(config) {}

    // Analyze terrain and generate river intensity map (0-1)
    // Call this after erosion to get water visualization
    std::unique_ptr<Heightmap> generateRiverMap(const Heightmap& terrain);

    // Generate lake map from depressions
    std::unique_ptr<Heightmap> generateLakeMap(const Heightmap& terrain);

    // Combined water map (rivers + lakes)
    std::unique_ptr<Heightmap> generateWaterMap(const Heightmap& terrain);

    Config& config() { return m_config; }

private:
    Config m_config;

    // D8 flow direction calculation
    void calculateFlowDirections(const Heightmap& terrain,
                                  std::vector<int>& flowDir);

    // Flow accumulation using D8
    void calculateFlowAccumulation(const Heightmap& terrain,
                                    const std::vector<int>& flowDir,
                                    Heightmap& flowAcc);

    // Detect valleys using curvature
    void calculateValleyDepth(const Heightmap& terrain,
                               Heightmap& valleyDepth);

    // Find depressions/lakes
    void findDepressions(const Heightmap& terrain,
                          Heightmap& lakeMap);

    // Smooth the river map
    void smoothMap(Heightmap& map, int passes);
};

} // namespace worldgen
