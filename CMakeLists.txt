cmake_minimum_required(VERSION 3.24)
project(fps_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  SDL2
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-2.30.12
)
FetchContent_MakeAvailable(SDL2)

FetchContent_Declare(
  bx
  GIT_REPOSITORY https://github.com/bkaradzic/bx.git
  GIT_TAG master
)
FetchContent_Declare(
  bimg
  GIT_REPOSITORY https://github.com/bkaradzic/bimg.git
  GIT_TAG master
)
FetchContent_Declare(
  bgfx
  GIT_REPOSITORY https://github.com/bkaradzic/bgfx.git
  GIT_TAG master
)
FetchContent_MakeAvailable(bx bimg bgfx)

set(BX_DIR "${bx_SOURCE_DIR}" CACHE PATH "" FORCE)
set(BIMG_DIR "${bimg_SOURCE_DIR}" CACHE PATH "" FORCE)
set(BGFX_DIR "${bgfx_SOURCE_DIR}" CACHE PATH "" FORCE)

set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  bgfx_cmake
  GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
  GIT_TAG master
)
FetchContent_GetProperties(bgfx_cmake POPULATED bgfx_cmake_populated)
if(NOT bgfx_cmake_populated)
  FetchContent_Populate(bgfx_cmake)
  file(READ "${bgfx_cmake_SOURCE_DIR}/cmake/bgfxToolUtils.cmake" _bgfx_tool_utils_text)
  string(
    REPLACE "O \"$<IF:$<CONFIG:Debug>:0,3>\""
    "O \"$<IF:$<CONFIG:Debug>,0,3>\""
    _bgfx_tool_utils_text
    "${_bgfx_tool_utils_text}"
  )
  file(WRITE "${bgfx_cmake_SOURCE_DIR}/cmake/bgfxToolUtils.cmake" "${_bgfx_tool_utils_text}")
endif()
add_subdirectory("${bgfx_cmake_SOURCE_DIR}" "${bgfx_cmake_BINARY_DIR}")

include("${bgfx_cmake_SOURCE_DIR}/cmake/bgfxToolUtils.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/engine_shaders.cmake")

FetchContent_Declare(
  EnTT
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v3.13.2
)
FetchContent_MakeAvailable(EnTT)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

set(SHADER_GEN_DIR "${CMAKE_BINARY_DIR}/generated/shaders")

add_executable(fps_engine
  src/main.cpp
  src/audio.cpp
  src/fps_camera.cpp
  src/physics_world.cpp
  src/level_data.cpp
  src/json_level.cpp
  src/level_mesh.cpp
  src/level_binary.cpp
  src/shader_program.cpp
  src/texture_loader.cpp
  src/viewmodel.cpp
  src/window.cpp
  src/hud.cpp
  src/ecs.cpp
  shaders/vs_triangle.sc
  shaders/fs_triangle.sc
  shaders/vs_skinned.sc
  shaders/fs_skinned.sc
  shaders/vs_debug.sc
  shaders/fs_debug.sc
  shaders/vs_hud.sc
  shaders/fs_hud.sc
)

target_compile_definitions(fps_engine PRIVATE
  "ENGINE_TEXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/textures\""
  "ENGINE_MAPS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/maps\""
  "ENGINE_MODELS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/models\""
  "ENGINE_SOUNDS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/sounds\""
  "ENGINE_FONTS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fonts\""
)

target_include_directories(fps_engine PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/external"
  "${CMAKE_CURRENT_SOURCE_DIR}/external/cgltf"
  "${SHADER_GEN_DIR}"
)

target_link_libraries(fps_engine PRIVATE
  SDL2::SDL2main
  SDL2::SDL2-static
  bgfx
  bx
  bimg
  bimg_decode
  EnTT::EnTT
  nlohmann_json::nlohmann_json
)

add_executable(level_pack
  src/tools/level_pack.cpp
  src/level_data.cpp
  src/json_level.cpp
  src/level_binary.cpp
)

target_include_directories(level_pack PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(level_pack PRIVATE
  nlohmann_json::nlohmann_json
)

if(APPLE)
  target_link_libraries(fps_engine PRIVATE "-framework Cocoa")
  # We only embed metal/glsl for macOS; silence SPIRV/WGSL/ESSL slots in BGFX_EMBEDDED_SHADER.
  # ESSL is dropped because the skinned shader needs dynamic uniform array indexing in the
  # vertex shader, which isn't permitted in GLSL ES 1.00.
  target_compile_definitions(
    fps_engine
    PRIVATE
      BGFX_PLATFORM_SUPPORTS_SPIRV=0
      BGFX_PLATFORM_SUPPORTS_WGSL=0
      BGFX_PLATFORM_SUPPORTS_ESSL=0
  )
endif()

engine_add_triangle_shaders(fps_engine)
