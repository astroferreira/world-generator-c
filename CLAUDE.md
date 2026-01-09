# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Goal

Build an **extremely realistic terrain/world/planet generator** with fractal level of detail. The system should support seamless zoom from planetary scale down to fine details like sand dunes with wind simulation. Each zoom level should reveal appropriate detail:

- **Planetary**: Continents, ocean basins, mountain ranges
- **Regional**: Valleys, river networks, forests, biomes
- **Local**: Hills, rocks, vegetation patterns
- **Micro**: Sand dunes, ripples, pebbles with physical simulations (wind, water)

Realism is the primary objective. All terrain features should emerge from physically-based simulations rather than purely procedural decoration.

## Build Commands

```bash
# Build (release)
./build.sh

# Build options
./build.sh -d           # Debug build
./build.sh -c           # Clean build
./build.sh -c -d        # Clean debug build
./build.sh -j 8         # Specify parallel jobs

# Manual CMake build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(sysctl -n hw.ncpu)
```

**Dependencies:** SDL2 (`brew install sdl2`), optionally OpenMP (`brew install libomp`)

## Architecture

### Core Data Flow

```
NoiseGenerator → TerrainGenerator → TerrainData → SimulationManager → Renderer
```

1. **TerrainGenerator** creates initial terrain by blending multiple noise layers (using FastNoiseLite) and applies an island mask for natural coastlines
2. **TerrainData** holds three heightmaps: `height`, `sediment`, and `water` - simulations modify these in-place
3. **SimulationManager** runs simulations sequentially (thermal erosion first, then hydraulic)
4. **HeightmapView** renders to SDL texture using ColorMapper's precomputed LUT for performance

### Simulation System

Simulations implement `ISimulation` interface (`src/simulation/ISimulation.hpp`):
- `initialize(TerrainData&)` - bind to terrain
- `step()` - single iteration
- `isComplete()` - signals when to advance to next simulation

**ThermalErosion**: Material slides down slopes exceeding talus angle. Completes when no changes occur.

**HydraulicErosion**: Particle-based droplet simulation with precomputed erosion brush for performance. Uses bilinear interpolation for sub-pixel accuracy.

### Key Classes

- **Heightmap** (`src/terrain/Heightmap.hpp`): 2D float grid with interpolation and gradient calculation
- **Application** (`src/core/Application.hpp`): Main loop, event handling, orchestrates all components
- **ColorMapper** (`src/renderer/ColorMapper.hpp`): Height-to-color gradient with LUT optimization

### Namespace

All code is in the `worldgen` namespace.

### External Libraries (header-only)

- `external/FastNoiseLite/` - Noise generation
- `external/stb/` - PNG export (stb_image_write)

## Visual Verification

Always verify terrain changes visually. After implementing or modifying features:

1. **Build and run** the application:
   ```bash
   ./build.sh && ./build/worldgen
   ```

2. **Export test images** by pressing `E` in the app (saves `terrain.png`), or run headless exports if implemented

3. **Evaluate against realism criteria**:
   - Does the terrain look natural at the target scale?
   - Are there artifacts (tiling, unrealistic patterns, hard edges)?
   - Do erosion features match real-world references (river valleys, alluvial fans, talus slopes)?
   - Is the level of detail appropriate for the zoom level?

4. **Compare before/after** when modifying simulations - export images before changes to compare

5. **Reference real terrain**: Compare outputs against satellite imagery, DEMs, or geological references for the feature being implemented

Keep test images organized (e.g., `test_outputs/`) for regression testing visual quality.

## Task Tracking

See `TODO.md` for the roadmap of features and improvements. Update it as tasks are completed or new requirements emerge.
