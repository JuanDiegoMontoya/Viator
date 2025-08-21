cmake_minimum_required(VERSION 3.25)

set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "")

project(Frogfood)

set(CMAKE_CXX_STANDARD 23)

option(FROGRENDER_FSR2_ENABLE "Enable FidelityFX Super Resolution 2. Windows only!" FALSE)

find_package(Vulkan REQUIRED)

set(viator_client_files
    vendor/stb_image.cpp
    vendor/tiny_obj_loader.cpp
    vendor/miniaudio.cpp
    vendor/ma_reverb_node.cpp
    src/Client/techniques/Bloom.h
    src/Client/techniques/Bloom.cpp
    src/Client/RendererUtilities.h
    src/Client/RendererUtilities.cpp
    src/Client/techniques/AutoExposure.h
    src/Client/techniques/AutoExposure.cpp
    src/Client/Fvog/Timer2.h
    src/Client/Fvog/Timer2.cpp
    src/Client/Fvog/Shader2.h
    src/Client/Fvog/Shader2.cpp
    src/Client/Fvog/detail/Common.h
    src/Client/Fvog/detail/Common.cpp
    src/Client/Fvog/Pipeline2.h
    src/Client/Fvog/Pipeline2.cpp
    src/Client/Fvog/BasicTypes2.h
    src/Client/Fvog/Texture2.h
    src/Client/Fvog/Texture2.cpp
    src/Client/Fvog/Device.h
    src/Client/Fvog/Device.cpp
    src/Client/Fvog/Rendering2.cpp
    src/Client/Fvog/Rendering2.h
    src/Client/Fvog/detail/ApiToEnum2.h
    src/Client/Fvog/detail/ApiToEnum2.cpp
    src/Client/Fvog/Buffer2.h
    src/Client/Fvog/Buffer2.cpp
    src/Client/Fvog/detail/SamplerCache2.h
    src/Client/Fvog/detail/SamplerCache2.cpp
    src/Client/Fvog/detail/Hash2.h
    src/Client/Fvog/TriviallyCopyableByteSpan.h
    src/Client/ImGui/imgui_impl_fvog.cpp
    src/Client/ImGui/imgui_impl_fvog.h
    src/Client/Renderables.h
    src/Client/Fvog/AccelerationStructure.h
    src/Client/Fvog/AccelerationStructure.cpp
    src/Client/debug/ForwardRenderer.h
    src/Client/debug/ForwardRenderer.cpp
    src/Client/debug/Shapes.h
    src/Client/techniques/ao/RayTracedAO.h
    src/Client/techniques/ao/RayTracedAO.cpp
    src/Client/techniques/denoising/spatial/Bilateral.h
    src/Client/techniques/denoising/spatial/Bilateral.cpp
    src/Client/techniques/volumetric/FroxelFog.h
    src/Client/techniques/volumetric/FroxelFog.cpp
    src/Client/PipelineManager.h
    src/Client/PipelineManager.cpp
    src/Client/VoxelRenderer.h
    src/Client/VoxelRenderer.cpp
    src/Client/PlayerHead.h
    src/Client/PlayerHead.cpp
    src/Client/Input.h
    src/Client/Input.cpp
    src/Client/Fvog/Descriptor.h
    src/Client/Fvog/Descriptor.cpp
    src/Client/Fvog/detail/VmaFwd.h
    src/Client/Fvog/detail/VkFwd.h
    src/Client/Gui.cpp
    src/Client/GuiHelpers.cpp
    src/Client/GuiHelpers.h
    src/Client/Editor.cpp
    src/Client/Editor.h
    src/Client/PlayerAudio.h
    src/Client/PlayerAudio.cpp
)

add_executable(frogRender
    ${viator_client_files}
    vendor/zstd.cpp
    src/main.cpp
    src/Client/Fvog/detail/Flags.h
    src/PCG.h
    src/Game/Game.cpp
    src/Game/Game.h
    src/Game/ClassImplMacros.h
    src/Game/Assets.h
    src/Game/Assets.cpp
    src/Game/TwoLevelGrid.h
    src/Game/TwoLevelGrid.cpp
    src/Game/SketchyBuffer.h
    src/Game/SketchyBuffer.cpp
    src/Game/Physics/Physics.h
    src/Game/Physics/Physics.cpp
    src/Game/Physics/TwoLevelGridShape.h
    src/Game/Physics/TwoLevelGridShape.cpp
    src/Game/Physics/PhysicsUtils.h
    src/Game/Physics/PhysicsUtils.cpp
    src/Game/Physics/DebugRenderer.h
    src/Game/Physics/DebugRenderer.cpp
    src/Game/Pathfinding.h
    src/Game/Pathfinding.cpp
    src/Game/LRUCache.h
    src/Game/HashGrid.h
    src/Game/Prefab.h
    src/Game/Prefab.cpp
    src/Game/VoxelType.h
    src/Game/Networking/Server.h
    src/Game/Networking/Client.h
    src/Game/Networking/EnetFwd.h
    src/Game/Networking/ThreadSafeQueue.h
    src/Game/Networking/Server.cpp
    src/Game/Networking/Interface.h
    src/Game/Networking/Client.cpp
    src/Game/Networking/RPC.h
    src/Game/Networking/RPC.cpp
    src/Game/Networking/Interface.cpp
    src/HashUtilities.h
    src/Core/Reflection.h
    src/Core/Reflection.cpp
    src/MathUtilities.cpp
    src/Core/Serialization.h
    src/Core/Serialization.cpp
    src/Core/Logging.h
    src/Core/Logging.cpp
    src/Core/Assert2.h
    src/Core/Assert2.cpp
    src/Core/Exception.h
    src/Game/VoxLoader.h
    src/Game/VoxLoader.cpp
    src/Game/EntityPrefab.h
    src/Game/EntityPrefab.cpp
    src/Game/Item.h
    src/Game/Item.cpp
    src/Game/Block.h
    src/Game/Block.cpp
    src/Game/BlockFwd.h
    src/Game/ItemFwd.h
    src/Game/EntityPrefabFwd.h
    src/Game/World.h
    src/Game/World.cpp
    src/Game/RegistryProxy.h
    src/Game/WorldInit.cpp
    src/Game/Head.h
    src/Game/Head.cpp
    src/Game/Audio.h
    src/Game/Audio.cpp
    src/Game/Physics/Shape.h
    src/Game/Scripting.h
    src/Game/Scripting.cpp
)

# Precompiled headers for standard library
target_precompile_headers(frogRender PRIVATE 
    <algorithm>
#    <array>
#    <bit>
    <chrono>
    <filesystem>
    <functional>
    <memory>
#    <optional>
    <string>
#    <span>
#    <unordered_map>
#    <unordered_set>
#    <variant>
#    <vector>
)

target_compile_options(frogRender
    PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
    -Wall
    -Wextra
    -Wpedantic
    -pedantic-errors
    -Wno-missing-field-initializers
    -Wno-unsafe-buffer-usage
    -Wno-pre-c++17-compat
    -Wno-c++98-compat
    -Wno-c++98-compat-pedantic
    -Wno-c++98-c++11-compat-binary-literal
    -Wno-c++98-compat-extra-semi
    -Wno-cast-function-type-strict # vkb generates a lot of these warnings
    -Wno-documentation # glfw generates these
    -Wno-documentation-unknown-command # glfw
    -Wno-double-promotion # stbi
    -Wno-old-style-cast
    -Wno-unused-result
    -ftime-trace
    -march=native # zstd requires BMI2 for precisely one function
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:
    -DFROG_DEBUG
    -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG
    >
    >
    $<$<CXX_COMPILER_ID:MSVC>:
    /bigobj # Reflection.cpp generates a lot of symbols.
    /W4
    /WX
    /permissive-
    /Zc:preprocessor # Enable preprocessor compliance
    /wd4324 # structure was padded
    /wd4127 # constant expression in conditional
    /wd4459 # declaration of 'foo' hides global declaration (from spdlog)
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:
    #/JMC # Has a non-negligible perf impact.
    #/UNDEBUG # Undefine NDEBUG for DebugFast builds. Causes linker errors with Jolt.
    /ZI          # Program Database for Edit and Continue
    /INCREMENTAL # Incremental linking is required for Edit and Continue
    /DFROG_DEBUG
    /DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG
    >
    >
)

option(FROGRENDER_DISABLE_OPTS "Disable compiler optimizations for release builds" FALSE)
if (MSVC AND FROGRENDER_DISABLE_OPTS)
    target_compile_options(frogRender PRIVATE -Od)
endif()

option(FROGRENDER_FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." TRUE)
if (${FORCE_COLORED_OUTPUT})
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
       add_compile_options(-fdiagnostics-color=always)
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
       add_compile_options(-fcolor-diagnostics)
    endif()
endif()

add_subdirectory(external)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/angelscript/sdk/angelscript/projects/cmake)

add_library(as_addons
    external/angelscript/sdk/add_on/scriptstdstring/scriptstdstring.cpp
    external/angelscript/sdk/add_on/scriptstdstring/scriptstdstring_utils.cpp
    external/angelscript/sdk/add_on/scriptbuilder/scriptbuilder.cpp
)
target_include_directories(as_addons PRIVATE 
    external/angelscript/sdk/angelscript/include
)
target_link_libraries(as_addons PRIVATE angelscript)
target_compile_options(as_addons PRIVATE "$<$<C_COMPILER_ID:MSVC>:/utf-8>")
target_compile_options(as_addons PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

if (FROGRENDER_FSR2_ENABLE)
    set(FSR2_LIBS ffx_fsr2_api_x64 ffx_fsr2_api_vk_x64)
    target_compile_definitions(frogRender PUBLIC FROGRENDER_FSR2_ENABLE)
else()
    set(FSR2_LIBS "")
endif()

target_compile_definitions(frogRender PUBLIC
    VMA_VULKAN_VERSION=1002000 # Allow VMA to use Vulkan 1.2 functions (BDA)
)

target_include_directories(frogRender
    PUBLIC
    ${FSR2_SOURCE}
    ${IMPLOT_SOURCE}
    ${Vulkan_INCLUDE_DIRS}
    vendor
    data
    src
    external/angelscript/sdk/angelscript/include
    external
    ${enet_SOURCE_DIR}/include # hack
)

target_link_libraries(frogRender
    PRIVATE
    glfw
    glm
    lib_imgui
    ${FSR2_LIBS}
    Tracy::TracyClient
    lib_implot
    volk::volk
    vk-bootstrap::vk-bootstrap
    VulkanMemoryAllocator
    glslang
    glslang-default-resource-limits
    SPIRV
    EnTT
    FastNoise
    unordered_dense
    choc
    cereal
    spdlog
    tomlplusplus::tomlplusplus
    rapidfuzz::rapidfuzz
    angelscript
    as_addons
)

target_compile_definitions(glm INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE VK_NO_PROTOTYPES GLFW_INCLUDE_NONE ImTextureID=ImU64)

if (MSVC)
    target_compile_definitions(frogRender PUBLIC STBI_MSC_SECURE_CRT)
else()
    target_link_libraries(frogRender PRIVATE tbb)
endif()
