#include "renderer/HeightmapView.hpp"
#include <stdexcept>

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
}

HeightmapView::~HeightmapView() {
    if (m_texture) {
        SDL_DestroyTexture(m_texture);
    }
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
    #pragma omp parallel for
    #endif
    for (size_t y = 0; y < h; ++y) {
        uint32_t* row = pixelData + (y * rowPixels);
        for (size_t x = 0; x < w; ++x) {
            float height = heightmap.get(x, y);
            row[x] = colorMapper.lookupFast(height);
        }
    }

    SDL_UnlockTexture(m_texture);
}

void HeightmapView::render(const SDL_Rect& destRect) {
    SDL_RenderCopy(m_renderer, m_texture, nullptr, &destRect);
}

} // namespace worldgen
