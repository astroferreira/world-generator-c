#pragma once

#include <SDL2/SDL.h>
#include "terrain/TerrainData.hpp"
#include "ColorMapper.hpp"
#include <vector>

namespace worldgen {

class HeightmapView {
public:
    HeightmapView(SDL_Renderer* renderer, size_t width, size_t height);
    ~HeightmapView();

    HeightmapView(const HeightmapView&) = delete;
    HeightmapView& operator=(const HeightmapView&) = delete;

    void update(const Heightmap& heightmap, const ColorMapper& colorMapper);
    void updateWithWater(const TerrainData& terrain, const ColorMapper& colorMapper);
    void render(const SDL_Rect& destRect);

    SDL_Texture* texture() const { return m_texture; }

    void setWaterEnabled(bool enabled) { m_waterEnabled = enabled; }
    bool waterEnabled() const { return m_waterEnabled; }

    // Dirty region tracking for partial updates
    void markDirty(int x, int y, int width, int height);
    void markFullDirty();
    void clearDirty();
    bool isDirty() const { return m_isDirty; }

    // Update only dirty regions (more efficient for incremental changes)
    void updateDirtyRegions(const Heightmap& heightmap, const ColorMapper& colorMapper);
    void updateDirtyRegionsWithWater(const TerrainData& terrain, const ColorMapper& colorMapper);

private:
    SDL_Renderer* m_renderer;
    SDL_Texture* m_texture;
    size_t m_width;
    size_t m_height;
    bool m_waterEnabled = true;

    // Dirty region tracking
    bool m_isDirty = true;
    SDL_Rect m_dirtyRect = {0, 0, 0, 0};

    // Pixel buffer for partial updates
    std::vector<uint32_t> m_pixelBuffer;

    void updateRegion(const Heightmap& heightmap, const ColorMapper& colorMapper,
                      int startX, int startY, int endX, int endY);
    void updateRegionWithWater(const TerrainData& terrain, const ColorMapper& colorMapper,
                               int startX, int startY, int endX, int endY);
};

} // namespace worldgen
