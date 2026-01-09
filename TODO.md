# TODO

Tasks to improve the world generator towards extremely realistic, fractal-level-of-detail terrain.

## Completed

- [x] Multi-continent generation with procedural noise-based landmasses
- [x] Adaptive sea-level calculation for target land ratio (~30%)
- [x] Organic coastlines via domain warping
- [x] Hydrology system with flow accumulation and river networks
- [x] Lake formation in terrain depressions (priority-flood algorithm)
- [x] Glacier system with elevation-based snow/ice
- [x] Glacial meltwater contribution to rivers
- [x] Seasonal water level variation (Spring/Summer/Autumn/Winter)
- [x] Water/ice/snow rendering with depth-based coloring
- [x] River channel erosion and sediment deposition
- [x] Delta formation at river mouths

## Multi-Scale / Level of Detail

- [ ] Implement quadtree or chunked terrain system for infinite zoom
- [ ] Add LOD-based detail generation (coarse at distance, fine up close)
- [ ] Seamless tile boundaries without visible seams
- [ ] Progressive detail loading/generation on zoom

## Erosion & Physical Simulations

- [x] Improve hydraulic erosion river formation (branching networks)
- [x] Add sediment deposition in floodplains and deltas
- [x] Basic glacial erosion under ice
- [ ] Implement wind erosion simulation
- [ ] Add aeolian dune formation (barchan, linear, star dunes)
- [ ] Coastal erosion and wave-cut platforms
- [ ] Advanced glacial erosion (U-valleys, cirques, moraines)
- [ ] Chemical weathering / karst terrain

## Geological Realism

- [ ] Tectonic plate simulation for continental shapes
- [ ] Fault lines and rift valleys
- [ ] Volcanic terrain (calderas, lava flows, shield vs stratovolcano)
- [ ] Sedimentary layering visible in cliffs
- [ ] Rock type differentiation affecting erosion rates

## Hydrology

- [x] Persistent river/lake system extraction from heightmap
- [x] Watershed and drainage basin calculation (flow accumulation)
- [ ] Realistic coastlines with bays, fjords, estuaries
- [ ] Underground water table affecting surface features
- [ ] River meandering over time
- [ ] Flooding simulation

## Biomes & Climate

- [ ] Climate simulation based on latitude, elevation, ocean currents
- [ ] Biome classification from temperature/precipitation
- [ ] Vegetation influence on erosion rates
- [ ] Soil type generation

## Rendering & Visualization

- [ ] Real-time zoom with detail generation
- [ ] 3D perspective view option
- [ ] Normal map generation for lighting
- [ ] Multiple export formats (heightmap, normal, biome masks)
- [ ] Headless batch rendering mode for testing

## Performance

- [ ] GPU acceleration (compute shaders for erosion)
- [ ] Multi-threaded chunk generation
- [ ] Caching system for generated detail levels
- [ ] Memory-efficient streaming for large worlds

## Near-Term Priorities

1. Wind erosion + dune simulation (key differentiator for micro-detail)
2. Quadtree LOD system (enables fractal zoom)
3. 3D visualization for better evaluation
4. River meandering and flooding dynamics
