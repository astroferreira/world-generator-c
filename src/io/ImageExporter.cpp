#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include "io/ImageExporter.hpp"
#include <vector>
#include <fstream>

namespace worldgen {

bool ImageExporter::exportPNG(const Heightmap& heightmap, const ColorMapper& colorMapper,
                               const std::string& filename) {
    const size_t w = heightmap.width();
    const size_t h = heightmap.height();

    std::vector<uint8_t> pixels(w * h * 4);

    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            Color c = colorMapper.getColor(heightmap.get(x, y));
            size_t idx = (y * w + x) * 4;
            pixels[idx + 0] = c.r;
            pixels[idx + 1] = c.g;
            pixels[idx + 2] = c.b;
            pixels[idx + 3] = c.a;
        }
    }

    return stbi_write_png(filename.c_str(), static_cast<int>(w), static_cast<int>(h),
                          4, pixels.data(), static_cast<int>(w * 4)) != 0;
}

bool ImageExporter::exportPNG(const Heightmap& heightmap, const std::string& filename) {
    ColorMapper grayscale(ColorMapper::Preset::Grayscale);
    return exportPNG(heightmap, grayscale, filename);
}

bool ImageExporter::exportRaw16(const Heightmap& heightmap, const std::string& filename) {
    const size_t w = heightmap.width();
    const size_t h = heightmap.height();

    std::ofstream file(filename, std::ios::binary);
    if (!file) return false;

    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            uint16_t value = static_cast<uint16_t>(heightmap.get(x, y) * 65535.0f);
            file.write(reinterpret_cast<const char*>(&value), sizeof(value));
        }
    }

    return true;
}

} // namespace worldgen
