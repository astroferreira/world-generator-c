#include "renderer/Window.hpp"
#include <stdexcept>

namespace worldgen {

Window::Window(const std::string& title, int width, int height)
    : m_width(width)
    , m_height(height)
{
    m_window = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!m_window) {
        throw std::runtime_error(std::string("Failed to create window: ") + SDL_GetError());
    }
}

Window::~Window() {
    if (m_window) {
        SDL_DestroyWindow(m_window);
    }
}

Window::Window(Window&& other) noexcept
    : m_window(other.m_window)
    , m_width(other.m_width)
    , m_height(other.m_height)
{
    other.m_window = nullptr;
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        if (m_window) {
            SDL_DestroyWindow(m_window);
        }
        m_window = other.m_window;
        m_width = other.m_width;
        m_height = other.m_height;
        other.m_window = nullptr;
    }
    return *this;
}

void Window::setTitle(const std::string& title) {
    SDL_SetWindowTitle(m_window, title.c_str());
}

} // namespace worldgen
