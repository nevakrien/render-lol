cmake_minimum_required(VERSION 3.22)
project(render_lol LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# All library sources are local; configuration never fetches dependencies.
if(ANDROID)
  # SDLActivity loads libSDL3.so before libmain.so.
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(3rd_party/SDL EXCLUDE_FROM_ALL)
set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX2D_DOCS OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_PROFILE OFF CACHE BOOL "" FORCE)
set(BOX2D_AVX2 OFF CACHE BOOL "" FORCE)
add_subdirectory(3rd_party/box2d EXCLUDE_FROM_ALL)

add_library(playground src/physics.cpp)
add_library(sound-mixer src/sound_mixer.cpp)
target_include_directories(sound-mixer PUBLIC src)
target_include_directories(playground PUBLIC src)
target_link_libraries(playground PUBLIC box2d)

find_package(Vulkan REQUIRED)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED NO_CMAKE_FIND_ROOT_PATH)
# Compile GLSL directly into the source shaders/ dir. This is both the runtime
# hot-reload dir the app watches AND the source the Python embed script uses for
# the in-binary fallback. Build (e.g. Sublime Ctrl+B) therefore produces the .spv.
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADERS background.vert background.frag mesh.vert body.frag effect.frag)
foreach(SHADER IN LISTS SHADERS)
  set(OUTPUT "${SHADER_DIR}/${SHADER}.spv")
  add_custom_command(OUTPUT "${OUTPUT}"
    COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.1 "${SHADER_DIR}/${SHADER}" -o "${OUTPUT}"
    DEPENDS "${SHADER_DIR}/${SHADER}" VERBATIM)
  list(APPEND SPIRV_FILES "${OUTPUT}")
endforeach()

# Embed the .spv bytes as an in-binary fallback so the app still runs even when
# the shaders/ dir is empty or unreachable.
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(SHADER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders.hpp")
add_custom_command(OUTPUT "${SHADER_HEADER}"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_shaders.py" "${SHADER_HEADER}" ${SPIRV_FILES}
  DEPENDS ${SPIRV_FILES} cmake/embed_shaders.py VERBATIM)

set(APP_SOURCES src/main.cpp src/audio.cpp src/render/vulkan.cpp src/render/passes.cpp "${SHADER_HEADER}")
if(ANDROID)
  add_library(main SHARED ${APP_SOURCES})
  set(APP_TARGET main)
else()
  add_executable(render-lol ${APP_SOURCES})
  set(APP_TARGET render-lol)
endif()
add_custom_target(shaders ALL DEPENDS ${SPIRV_FILES})
add_dependencies(${APP_TARGET} shaders)
target_include_directories(${APP_TARGET} PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated")  # for #include "shaders.hpp"
# Runtime hot-reload dir: this is the source shaders/ dir that the build populates.
if(ANDROID)
  target_compile_definitions(${APP_TARGET} PRIVATE RUNTIME_SHADER_DIR="")
else()
  target_compile_definitions(${APP_TARGET} PRIVATE
    RUNTIME_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
endif()
target_link_libraries(${APP_TARGET} PRIVATE playground sound-mixer SDL3::SDL3 Vulkan::Vulkan)
if(MSVC)
  target_compile_options(playground PRIVATE /W4)
  target_compile_options(${APP_TARGET} PRIVATE /W4)
else()
  target_compile_options(box2d PRIVATE -w)
  target_compile_options(playground PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(${APP_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING AND NOT ANDROID)
  add_executable(audio-test tests/audio_test.cpp)
  target_link_libraries(audio-test PRIVATE sound-mixer)
  add_test(NAME audio-mixing COMMAND audio-test)
endif()
