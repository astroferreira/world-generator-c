#pragma once

#include <SDL2/SDL.h>
#include "terrain/Heightmap.hpp"
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
    void render(const SDL_Rect& destRect);

    SDL_Texture* texture() const { return m_texture; }

private:
    SDL_Renderer* m_renderer;
    SDL_Texture* m_texture;
    size_t m_width;
    size_t m_height;
};

} // namespace worldgen
