#pragma once

#include "terrain/Heightmap.hpp"
#include "renderer/ColorMapper.hpp"
#include <string>

namespace worldgen {

class ImageExporter {
public:
    static bool exportPNG(const Heightmap& heightmap, const ColorMapper& colorMapper,
                          const std::string& filename);
    static bool exportPNG(const Heightmap& heightmap, const std::string& filename);
    static bool exportRaw16(const Heightmap& heightmap, const std::string& filename);
};

} // namespace worldgen
