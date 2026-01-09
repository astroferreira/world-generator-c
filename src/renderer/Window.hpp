#pragma once

#include <SDL2/SDL.h>
#include <string>

namespace worldgen {

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    SDL_Window* get() const { return m_window; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    void setTitle(const std::string& title);

private:
    SDL_Window* m_window = nullptr;
    int m_width;
    int m_height;
};

} // namespace worldgen
