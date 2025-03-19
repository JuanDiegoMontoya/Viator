# Viator

A voxel game with unrealistic ambitions.

## Tech

C++ and Vulkan.

### Storage

Voxels are stored in a simple two-level grid consisting of top-level and bottom-level chunks.
I sometimes call it a brickmap, but I haven't read the paper so I don't know if that's accurate.
Top- and bottom-level chunks both hold 8x8x8 (512) elements, but homogeneous chunks can be collapsed into a single value.
Bottom-level bricks additionally store a bitmask indicating the "occupancy" of each voxel. The occupancy bitmask is used to
improve cache coherency in ray tracing.

A fixed memory pool is employed for the grid structure. It is mirrored exactly on the GPU side, so propagating updates from 
the CPU to the GPU is as simple as marking which pages of the pool were touched by a modification, then copying them into 
the GPU-side representation.

### Rendering

Lighting is entirely path traced, with 1 sample per pixel, 2 bounces after the primary ray, and 1 NEE ray per bounce.
Rays traverse the voxel grid with hierarchical DDA. Meshes (for dynamic game entities) can be arbitrarily placed 
and lit, but do not themselves affect lighting.

Light is emitted by either voxels or by explicitly sampled local or directional lights.

A primitive spatial à-trous denoiser is used to make the image more tolerable to look at.

## Building

Get a modern version of CMake and do the `mkdir build && cd build && cmake ..` thing after cloning this repo. All dependencies are vendored or fetched with FetchContent. Should work on any sufficiently modern desktop GPU on Windows and Linux (I only test on Windows however).

## Dependencies

### Fetched

- [GLFW](https://github.com/glfw/glfw)
- [GLM](https://github.com/g-truc/glm)
- [volk](https://github.com/zeux/volk.git)
- [vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap)
- [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [glslang](https://github.com/KhronosGroup/glslang.git)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [ImPlot](https://github.com/epezent/implot.git)
- [FidelityFX Super Resolution 2](https://github.com/JuanDiegoMontoya/FidelityFX-FSR2.git) (my fork)
- [Tracy](https://github.com/wolfpld/tracy.git)
- [EnTT](https://github.com/skypjack/entt)
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics)
- [FastNoise2](https://github.com/Auburn/FastNoise2)
- [ankerl::unordered_dense](https://github.com/martinus/unordered_dense/)
- [CHOC](https://github.com/Tracktion/choc/)
- [cereal](https://uscilab.github.io/cereal/)

### Vendored

- [Material Design Icons](https://github.com/google/material-design-icons/)
- [Font Awesome 6](https://github.com/FortAwesome/Font-Awesome/)
- [stb_image.h](https://github.com/nothings/stb)
- [stb_include.h](https://github.com/nothings/stb) (the vendored version is heavily modified)
- [Tony McMapFace LUT](https://github.com/h3r2tic/tony-mc-mapface)
- [AgX shader](https://www.shadertoy.com/view/Dt3XDr)
- Probably several more code snippets that I've forgotten about

## Philosophy

No bikeshedding!
