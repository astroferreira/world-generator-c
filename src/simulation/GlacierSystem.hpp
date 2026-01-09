#pragma once

#include "ISimulation.hpp"
#include "HydrologyData.hpp"
#include "utils/Random.hpp"
#include "utils/Vec2.hpp"

namespace worldgen {

class GlacierSystem : public ISimulation {
public:
    GlacierSystem();
    explicit GlacierSystem(const GlacierParams& params);

    std::string name() const override { return "Glaciers"; }
    void initialize(TerrainData& terrain) override;
    void step() override;
    void reset() override;
    int iterations() const override { return m_iterations; }
    bool isComplete() const override;

    void setParams(const GlacierParams& params);
    const GlacierParams& params() const { return m_params; }

    float getIceThickness(size_t x, size_t y) const;
    float getSnowDepth(size_t x, size_t y) const;
    bool isGlaciated(size_t x, size_t y) const;
    bool hasSnow(size_t x, size_t y) const;

private:
    GlacierParams m_params;
    TerrainData* m_terrain = nullptr;
    Random m_random;
    int m_iterations = 0;

    // Thread-local ice flow buffers for lock-free parallel accumulation
    std::vector<std::vector<float>> m_threadLocalIceFlow;

    void accumulateSnow();
    void compactSnowToIce();
    void calculateMelt();
    void flowGlaciers();
    void erodeUnderGlaciers();
    void contributeMeltwater();

    float getTemperature(size_t x, size_t y) const;
    bool isAboveSnowline(size_t x, size_t y) const;
    Vec2f getGlacierFlowDirection(size_t x, size_t y) const;
};

} // namespace worldgen
