#include "renderer/HeightmapView.hpp"
#include "simulation/HydrologyData.hpp"
#include <stdexcept>
#include <algorithm>

#ifdef WORLDGEN_USE_OPENMP
#include <omp.h>
#endif

namespace worldgen {

HeightmapView::HeightmapView(SDL_Renderer* renderer, size_t width, size_t height)
    : m_renderer(renderer)
    , m_width(width)
    , m_height(height)
{
    m_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        static_cast<int>(width),
        static_cast<int>(height)
    );

    if (!m_texture) {
        throw std::runtime_error(std::string("Failed to create texture: ") + SDL_GetError());
    }

    // Pre-allocate pixel buffer for partial updates
    m_pixelBuffer.resize(width * height);
    markFullDirty();
}

HeightmapView::~HeightmapView() {
    if (m_texture) {
        SDL_DestroyTexture(m_texture);
    }
}

void HeightmapView::markDirty(int x, int y, int width, int height) {
    if (!m_isDirty) {
        m_dirtyRect = {x, y, width, height};
        m_isDirty = true;
    } else {
        // Expand existing dirty rect to include new region
        int minX = std::min(m_dirtyRect.x, x);
        int minY = std::min(m_dirtyRect.y, y);
        int maxX = std::max(m_dirtyRect.x + m_dirtyRect.w, x + width);
        int maxY = std::max(m_dirtyRect.y + m_dirtyRect.h, y + height);
        m_dirtyRect = {minX, minY, maxX - minX, maxY - minY};
    }
}

void HeightmapView::markFullDirty() {
    m_dirtyRect = {0, 0, static_cast<int>(m_width), static_cast<int>(m_height)};
    m_isDirty = true;
}

void HeightmapView::clearDirty() {
    m_isDirty = false;
    m_dirtyRect = {0, 0, 0, 0};
}

void HeightmapView::update(const Heightmap& heightmap, const ColorMapper& colorMapper) {
    void* pixels;
    int pitch;

    if (SDL_LockTexture(m_texture, nullptr, &pixels, &pitch) != 0) {
        return;
    }

    uint32_t* pixelData = static_cast<uint32_t*>(pixels);
    const size_t w = heightmap.width();
    const size_t h = heightmap.height();
    const int rowPixels = pitch / sizeof(uint32_t);

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 0; y < h; ++y) {
        uint32_t* row = pixelData + (y * rowPixels);
        for (size_t x = 0; x < w; ++x) {
            float height = heightmap.get(x, y);
            row[x] = colorMapper.lookupFast(height);
        }
    }

    SDL_UnlockTexture(m_texture);
    clearDirty();
}

void HeightmapView::updateWithWater(const TerrainData& terrain, const ColorMapper& colorMapper) {
    void* pixels;
    int pitch;

    if (SDL_LockTexture(m_texture, nullptr, &pixels, &pitch) != 0) {
        return;
    }

    uint32_t* pixelData = static_cast<uint32_t*>(pixels);
    const size_t w = terrain.width();
    const size_t h = terrain.heightDim();
    const int rowPixels = pitch / sizeof(uint32_t);

    const bool hasHydrology = terrain.hydrology != nullptr;
    const bool hasWater = terrain.water != nullptr;
    const bool waterEnabled = m_waterEnabled;

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t y = 0; y < h; ++y) {
        uint32_t* row = pixelData + (y * rowPixels);
        for (size_t x = 0; x < w; ++x) {
            float height = terrain.height->get(x, y);

            // Fast path: no water effects
            if (!waterEnabled) {
                row[x] = colorMapper.lookupFast(height);
                continue;
            }

            float waterDepth = hasWater ? terrain.water->get(x, y) : 0.0f;
            float iceThickness = 0.0f;
            float snowDepth = 0.0f;

            if (hasHydrology) {
                iceThickness = terrain.hydrology->iceThickness->get(x, y);
                snowDepth = terrain.hydrology->snowpack->get(x, y);
            }

            // Check if water effects needed (with threshold to avoid unnecessary color calc)
            if (waterDepth > 0.001f || iceThickness > 0.01f || snowDepth > 0.005f) {
                Color c = colorMapper.getColorWithWater(height, waterDepth, iceThickness, snowDepth);
                row[x] = c.toARGB();
            } else {
                row[x] = colorMapper.lookupFast(height);
            }
        }
    }

    SDL_UnlockTexture(m_texture);
    clearDirty();
}

void HeightmapView::updateDirtyRegions(const Heightmap& heightmap, const ColorMapper& colorMapper) {
    if (!m_isDirty) return;

    // Clamp dirty rect to texture bounds
    int startX = std::max(0, m_dirtyRect.x);
    int startY = std::max(0, m_dirtyRect.y);
    int endX = std::min(static_cast<int>(m_width), m_dirtyRect.x + m_dirtyRect.w);
    int endY = std::min(static_cast<int>(m_height), m_dirtyRect.y + m_dirtyRect.h);

    if (endX <= startX || endY <= startY) {
        clearDirty();
        return;
    }

    // For small updates, use partial texture update
    // For large updates (>25% of texture), do full update
    int dirtyArea = (endX - startX) * (endY - startY);
    int totalArea = static_cast<int>(m_width * m_height);

    if (dirtyArea > totalArea / 4) {
        update(heightmap, colorMapper);
        return;
    }

    updateRegion(heightmap, colorMapper, startX, startY, endX, endY);
    clearDirty();
}

void HeightmapView::updateDirtyRegionsWithWater(const TerrainData& terrain, const ColorMapper& colorMapper) {
    if (!m_isDirty) return;

    int startX = std::max(0, m_dirtyRect.x);
    int startY = std::max(0, m_dirtyRect.y);
    int endX = std::min(static_cast<int>(m_width), m_dirtyRect.x + m_dirtyRect.w);
    int endY = std::min(static_cast<int>(m_height), m_dirtyRect.y + m_dirtyRect.h);

    if (endX <= startX || endY <= startY) {
        clearDirty();
        return;
    }

    int dirtyArea = (endX - startX) * (endY - startY);
    int totalArea = static_cast<int>(m_width * m_height);

    if (dirtyArea > totalArea / 4) {
        updateWithWater(terrain, colorMapper);
        return;
    }

    updateRegionWithWater(terrain, colorMapper, startX, startY, endX, endY);
    clearDirty();
}

void HeightmapView::updateRegion(const Heightmap& heightmap, const ColorMapper& colorMapper,
                                  int startX, int startY, int endX, int endY) {
    const int regionW = endX - startX;
    const int regionH = endY - startY;

    // Fill pixel buffer for the region
    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (int y = startY; y < endY; ++y) {
        for (int x = startX; x < endX; ++x) {
            float height = heightmap.get(x, y);
            m_pixelBuffer[(y - startY) * regionW + (x - startX)] = colorMapper.lookupFast(height);
        }
    }

    // Update texture region
    SDL_Rect updateRect = {startX, startY, regionW, regionH};
    SDL_UpdateTexture(m_texture, &updateRect, m_pixelBuffer.data(), regionW * sizeof(uint32_t));
}

void HeightmapView::updateRegionWithWater(const TerrainData& terrain, const ColorMapper& colorMapper,
                                           int startX, int startY, int endX, int endY) {
    const int regionW = endX - startX;
    const int regionH = endY - startY;
    const bool hasHydrology = terrain.hydrology != nullptr;
    const bool hasWater = terrain.water != nullptr;
    const bool waterEnabled = m_waterEnabled;

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (int y = startY; y < endY; ++y) {
        for (int x = startX; x < endX; ++x) {
            float height = terrain.height->get(x, y);

            if (!waterEnabled) {
                m_pixelBuffer[(y - startY) * regionW + (x - startX)] = colorMapper.lookupFast(height);
                continue;
            }

            float waterDepth = hasWater ? terrain.water->get(x, y) : 0.0f;
            float iceThickness = 0.0f;
            float snowDepth = 0.0f;

            if (hasHydrology) {
                iceThickness = terrain.hydrology->iceThickness->get(x, y);
                snowDepth = terrain.hydrology->snowpack->get(x, y);
            }

            if (waterDepth > 0.001f || iceThickness > 0.01f || snowDepth > 0.005f) {
                Color c = colorMapper.getColorWithWater(height, waterDepth, iceThickness, snowDepth);
                m_pixelBuffer[(y - startY) * regionW + (x - startX)] = c.toARGB();
            } else {
                m_pixelBuffer[(y - startY) * regionW + (x - startX)] = colorMapper.lookupFast(height);
            }
        }
    }

    SDL_Rect updateRect = {startX, startY, regionW, regionH};
    SDL_UpdateTexture(m_texture, &updateRect, m_pixelBuffer.data(), regionW * sizeof(uint32_t));
}

void HeightmapView::render(const SDL_Rect& destRect) {
    SDL_RenderCopy(m_renderer, m_texture, nullptr, &destRect);
}

} // namespace worldgen
