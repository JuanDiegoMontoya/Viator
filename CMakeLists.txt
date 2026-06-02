cmake_minimum_required(VERSION 3.5)

project(Frogfood)

option(FROGRENDER_FSR2_ENABLE "Enable FidelityFX Super Resolution 2. Windows only!" FALSE)
option(GAME_HEADLESS "Build the game without graphics or input." FALSE)
option(GAME_COMPILE_TESTS "Compile the game's tests." TRUE)
option(GAME_FORCE_DEBUG_INFO "Compile the game with debug info." FALSE)
option(GAME_ENABLE_ASAN "Compile with address sanitizer." FALSE)

#TODO: Use an allocator that doesn't require Vulkan.
find_package(Vulkan REQUIRED)

if (NOT GAME_HEADLESS)
    set(viator_client_files
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
        src/Client/techniques/ao/ScreenSpaceGI.h
        src/Client/techniques/ao/ScreenSpaceGI.cpp
        src/Client/Gui/Console.h
        src/Client/Gui/Console.cpp
        src/Client/techniques/shadows/CascadedShadowMaps.h
        src/Client/techniques/shadows/CascadedShadowMaps.cpp
        src/Client/techniques/volumetric/RayMarchedClouds.h
        src/Client/techniques/volumetric/RayMarchedClouds.cpp
        src/Client/techniques/Sky.h
        src/Client/techniques/Sky.cpp
        src/Client/techniques/Particles.h
        src/Client/techniques/Particles.cpp
        src/Client/Scheduler.h
        src/Client/Scheduler.cpp
    )
endif()

add_executable(frogRender
    ${viator_client_files}
    src/Client/Fvog/detail/Common.h
    src/Client/Fvog/detail/Common.cpp
    src/main.cpp
    src/Client/Fvog/detail/Flags.h
    src/PCG.h
    src/Game/Game.cpp
    src/Game/Game.h
    src/Core/ClassImplMacros.h
    src/Game/Assets.h
    src/Game/Assets.cpp
    src/Game/Voxel/Grid.h
    src/Game/Voxel/Grid.cpp
    src/Game/Voxel/SketchyBuffer.h
    src/Game/Voxel/SketchyBuffer.cpp
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
    src/Game/Voxel/VoxelType.h
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
    src/Game/Networking/RpcInfo.h
    src/Game/TestGame.h
    src/Game/TestGame.cpp
    src/Game/Networking/TestNetworking.cpp
    src/Game/Systems/CharacterController.h
    src/Game/Systems/CharacterController.cpp
    src/Game/Systems/CharacterAI.h
    src/Game/Systems/CharacterAI.cpp
    src/Game/WorldFixedUpdate.cpp
    src/Game/WorldGenerateMap.cpp
    src/Core/ReflectionMacros.h
    src/Core/ReflectionMacrosInternal.h
    src/Game/CoreComponents.h
    src/Core/Container/RingBuffer.h
    src/Core/Container/RingBuffer.cpp
    src/Core/Image.h
    src/Core/Image.cpp
    src/Core/Container/BitArray.h
    src/Core/Container/BitArray.cpp
    src/Game/Globals.h
    src/Game/Globals.cpp
    src/Game/IncompleteTypeDeleter.h
    src/Game/IncompleteTypeDeleter.cpp
    src/Game/World/SurfaceBiome.h
    src/Game/World/SurfaceBiome.cpp
    src/Game/World/UndergroundBiome.h
    src/Game/World/UndergroundBiome.cpp
    src/Game/World/WorldGenHelpers.h
    src/Game/World/WorldGenHelpers.cpp
    src/Game/Commands.h
    src/Game/Commands.cpp
    src/Core/StringUtilities.h
    src/Core/StringUtilities.cpp
    src/Game/CommandParser.h
    src/Game/CommandParser.cpp
    src/Game/CVar.h
    src/Game/CVar.cpp
    src/Game/CVarInternal.h
    src/Game/Rendering/Particle.h
    src/Core/Timer.h
    src/Core/Timer.cpp
    src/Core/Defer.h
    src/Core/Platform/PlatformInit.h
    src/Core/Platform/PlatformInit.cpp
    src/Core/Platform/Sleep.h
    src/Core/Platform/Sleep.cpp
)

target_compile_features(frogRender PUBLIC cxx_std_23)

if (NOT GAME_ENABLE_ASAN)
    set(EDIT_AND_CONTINUE_FLAGS "/ZI /INCREMENTAL") # Flags incompatible with /fsanitize=address
else()
    set(EDIT_AND_CONTINUE_FLAGS "")
endif()

target_compile_options(frogRender
    PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
    #-Wall
    #-Wextra
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
    -Wno-float-equal # Generated when =default'ing classes with float members.
    -Wno-newline-eof
    -Wno-format-nonliteral
    -Wunused-variable
    -march=native # zstd requires BMI2 for precisely one function
    #-ftime-trace # Emit flamegraph traces (viewable in chrome://tracing and speedscope.app) for each TU.
    #-fproc-stat-report # Print memory and execution time for each compilation step.
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
    ${EDIT_AND_CONTINUE_FLAGS}
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

if (GAME_FORCE_DEBUG_INFO)
    if (MSVC)
        target_compile_options(frogRender PRIVATE /Zi)
        target_link_options(frogRender PRIVATE /DEBUG)
    else()
        target_compile_options(frogRender PRIVATE -g)
    endif()
endif()

if (GAME_ENABLE_ASAN)
    target_compile_options(frogRender PRIVATE -fsanitize=address)
    add_compile_options(-fsanitize=address)
    if (NOT MSVC)
        target_link_options(frogRender PRIVATE -fsanitize=address)
        add_link_options(-fsanitize=address)
    endif()
endif()

# Precompiled headers for standard library
if (NOT GAME_ENABLE_ASAN) # ASan and PCH don't play well together on MSVC, for some reason.
    target_precompile_headers(frogRender PRIVATE 
        <algorithm>
    #    <array>
    #    <bit>
        <chrono>
        <filesystem>
        <functional>
        <memory>
        <optional>
        <string>
        <span>
        <unordered_map>
        <unordered_set>
        <variant>
        <vector>
    )
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

if (NOT GAME_COMPILE_TESTS)
    target_compile_definitions(frogRender PUBLIC DOCTEST_CONFIG_DISABLE)
endif()

if (GAME_HEADLESS)
    target_compile_definitions(frogRender PUBLIC GAME_HEADLESS)
endif()

target_compile_definitions(frogRender PUBLIC
    VMA_VULKAN_VERSION=1002000 # Allow VMA to use Vulkan 1.2 functions (BDA)
    DOCTEST_CONFIG_NO_UNPREFIXED_OPTIONS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    VK_NO_PROTOTYPES
    GLFW_INCLUDE_NONE
    ImTextureID=ImU64
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

add_library(miniaudio vendor/miniaudio.cpp vendor/ma_reverb_node.cpp)
add_library(zstd vendor/zstd.cpp)
target_compile_definitions(zstd PRIVATE ZSTD_ASAN_DONT_POISON_WORKSPACE) # on MSVC, zstd attempts to call some undefined functions when ASan is enabled.

if (NOT GAME_HEADLESS)
    add_library(stbi vendor/stb_image.cpp)
    add_library(tinyobj vendor/tiny_obj_loader.cpp)
    set(libraries_for_head
        glfw
        lib_imgui
        lib_implot
        ${FSR2_LIBS}
        volk::volk
        vk-bootstrap::vk-bootstrap
        glslang::glslang
        glslang::glslang-default-resource-limits
        glslang::SPIRV
        stbi
        miniaudio
        tinyobj
    )
endif()

target_link_libraries(frogRender
    PRIVATE
    ${libraries_for_head}
    glm::glm
    Tracy::TracyClient
    VulkanMemoryAllocator # VMA is headless builds because its virtual allocator is used in server logic
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
    Jolt
    zstd
)

if (MSVC)
    target_compile_definitions(frogRender PUBLIC STBI_MSC_SECURE_CRT)
else()
    target_link_libraries(frogRender PRIVATE tbb)
endif()
