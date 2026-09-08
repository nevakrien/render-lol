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
file(GLOB QUARK_SOURCES CONFIGURE_DEPENDS
  3rd_party/quarkphysics/QuarkPhysics/*.cpp
  3rd_party/quarkphysics/QuarkPhysics/json/*.cpp
  3rd_party/quarkphysics/QuarkPhysics/polypartition/*.cpp)
add_library(quarkphysics STATIC ${QUARK_SOURCES})
target_include_directories(quarkphysics SYSTEM PUBLIC 3rd_party/quarkphysics/QuarkPhysics)

add_library(playground src/physics.cpp)
add_library(sound-mixer src/sound_mixer.cpp)
target_include_directories(sound-mixer PUBLIC src)
target_include_directories(playground PUBLIC src)
target_link_libraries(playground PUBLIC quarkphysics)

find_package(Vulkan REQUIRED)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED NO_CMAKE_FIND_ROOT_PATH)
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(SHADERS background.vert background.frag mesh.vert body.frag effect.frag)
set(SPIRV_FILES)
foreach(SHADER IN LISTS SHADERS)
  set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER}.spv")
  add_custom_command(OUTPUT "${OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.1 "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}" -o "${OUTPUT}"
    DEPENDS "shaders/${SHADER}" VERBATIM)
  list(APPEND SPIRV_FILES "${OUTPUT}")
endforeach()
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
target_include_directories(${APP_TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(${APP_TARGET} PRIVATE playground sound-mixer SDL3::SDL3 Vulkan::Vulkan)
if(MSVC)
  target_compile_options(playground PRIVATE /W4)
  target_compile_options(${APP_TARGET} PRIVATE /W4)
else()
  target_compile_options(quarkphysics PRIVATE -w)
  target_compile_options(playground PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(${APP_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING AND NOT ANDROID)
  add_executable(physics-test tests/physics_test.cpp)
  target_link_libraries(physics-test PRIVATE playground)
  add_test(NAME physics COMMAND physics-test)
  add_executable(audio-test tests/audio_test.cpp)
  target_link_libraries(audio-test PRIVATE sound-mixer)
  add_test(NAME audio-mixing COMMAND audio-test)
endif()
