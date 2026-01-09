#pragma once

#include "terrain/TerrainData.hpp"
#include <string>

namespace worldgen {

class ISimulation {
public:
    virtual ~ISimulation() = default;

    virtual std::string name() const = 0;
    virtual void initialize(TerrainData& terrain) = 0;
    virtual void step() = 0;
    virtual void reset() = 0;
    virtual int iterations() const = 0;
    virtual bool isComplete() const { return false; }

    // Continuous simulations run alongside other simulations
    // (e.g., hydrology updates as terrain changes from erosion)
    virtual bool isContinuous() const { return false; }
};

} // namespace worldgen
