cmake_minimum_required(VERSION 3.16)
project(WorldGenerator
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "2D World Generation Engine with Erosion Simulation"
)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type defaults to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Options
option(WORLDGEN_ENABLE_OPENMP "Enable OpenMP parallelization" ON)

# Find SDL2
find_package(SDL2 REQUIRED)

# Find OpenMP (optional) - with macOS Homebrew support
if(WORLDGEN_ENABLE_OPENMP)
    if(APPLE)
        # Help CMake find libomp from Homebrew
        execute_process(
            COMMAND brew --prefix libomp
            OUTPUT_VARIABLE LIBOMP_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(LIBOMP_PREFIX)
            set(OpenMP_C_FLAGS "-Xpreprocessor -fopenmp -I${LIBOMP_PREFIX}/include")
            set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I${LIBOMP_PREFIX}/include")
            set(OpenMP_C_LIB_NAMES "omp")
            set(OpenMP_CXX_LIB_NAMES "omp")
            set(OpenMP_omp_LIBRARY "${LIBOMP_PREFIX}/lib/libomp.dylib")
        endif()
    endif()
    find_package(OpenMP)
endif()

# Source files
set(WORLDGEN_SOURCES
    src/main.cpp
    src/core/Application.cpp
    src/terrain/Heightmap.cpp
    src/generation/NoiseGenerator.cpp
    src/generation/TerrainGenerator.cpp
    src/simulation/SimulationManager.cpp
    src/simulation/HydraulicErosion.cpp
    src/simulation/ThermalErosion.cpp
    src/simulation/HydrologySimulation.cpp
    src/simulation/GlacierSystem.cpp
    src/renderer/Window.cpp
    src/renderer/HeightmapView.cpp
    src/renderer/ColorMapper.cpp
    src/renderer/TerrainShader.cpp
    src/renderer/ObliqueRenderer.cpp
    src/io/ImageExporter.cpp
)

# Main executable
add_executable(worldgen ${WORLDGEN_SOURCES})

# Include directories
target_include_directories(worldgen PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
    ${SDL2_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(worldgen PRIVATE
    ${SDL2_LIBRARIES}
)

# OpenMP support
if(OpenMP_CXX_FOUND)
    target_link_libraries(worldgen PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(worldgen PRIVATE WORLDGEN_USE_OPENMP)
    message(STATUS "OpenMP enabled")
else()
    message(STATUS "OpenMP not found, parallelization disabled")
endif()

# Compiler-specific flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(worldgen PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-g -O0>
    )
elseif(MSVC)
    target_compile_options(worldgen PRIVATE
        /W4
        $<$<CONFIG:Release>:/O2>
    )
endif()

# macOS-specific: Handle SDL2 framework
if(APPLE)
    target_link_libraries(worldgen PRIVATE "-framework Cocoa")
endif()
