# TODO

Tasks to improve the world generator towards extremely realistic, fractal-level-of-detail terrain.

## Completed

- [x] Multi-continent generation with procedural noise-based landmasses
- [x] Adaptive sea-level calculation for target land ratio (~30%)
- [x] Organic coastlines via domain warping

## Multi-Scale / Level of Detail

- [ ] Implement quadtree or chunked terrain system for infinite zoom
- [ ] Add LOD-based detail generation (coarse at distance, fine up close)
- [ ] Seamless tile boundaries without visible seams
- [ ] Progressive detail loading/generation on zoom

## Erosion & Physical Simulations

- [ ] Improve hydraulic erosion river formation (branching networks, meanders)
- [ ] Add sediment deposition in floodplains and deltas
- [ ] Implement wind erosion simulation
- [ ] Add aeolian dune formation (barchan, linear, star dunes)
- [ ] Coastal erosion and wave-cut platforms
- [ ] Glacial erosion (U-valleys, cirques, moraines)
- [ ] Chemical weathering / karst terrain

## Geological Realism

- [ ] Tectonic plate simulation for continental shapes
- [ ] Fault lines and rift valleys
- [ ] Volcanic terrain (calderas, lava flows, shield vs stratovolcano)
- [ ] Sedimentary layering visible in cliffs
- [ ] Rock type differentiation affecting erosion rates

## Hydrology

- [ ] Persistent river/lake system extraction from heightmap
- [ ] Watershed and drainage basin calculation
- [ ] Realistic coastlines with bays, fjords, estuaries
- [ ] Underground water table affecting surface features

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
3. Improved river network extraction
4. 3D visualization for better evaluation
