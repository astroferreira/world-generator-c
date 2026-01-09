#pragma once

#include "Heightmap.hpp"
#include "simulation/HydrologyData.hpp"
#include <memory>

namespace worldgen {

struct TerrainData {
    std::unique_ptr<Heightmap> height;
    std::unique_ptr<Heightmap> sediment;
    std::unique_ptr<Heightmap> water;

    // Hydrology extension (lazily initialized)
    std::unique_ptr<HydrologyState> hydrology;

    size_t width() const { return height ? height->width() : 0; }
    size_t heightDim() const { return height ? height->height() : 0; }

    float totalHeight(size_t x, size_t y) const {
        float h = height->get(x, y);
        if (sediment) h += sediment->get(x, y);
        return h;
    }

    // Check if cell has water
    bool hasWater(size_t x, size_t y) const {
        return water && water->get(x, y) > 0.001f;
    }

    // Get water depth
    float waterDepth(size_t x, size_t y) const {
        return water ? water->get(x, y) : 0.0f;
    }

    static TerrainData create(size_t width, size_t height) {
        TerrainData data;
        data.height = std::make_unique<Heightmap>(width, height);
        data.sediment = std::make_unique<Heightmap>(width, height);
        data.water = std::make_unique<Heightmap>(width, height);
        return data;
    }
};

} // namespace worldgen
