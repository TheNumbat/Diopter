cmake_minimum_required(VERSION 3.22)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CLANG TRUE)
endif()

set(RELEASE_BUILD
    $<$<CONFIG:Debug>:FALSE>
    $<$<CONFIG:RelWithDebInfo>:FALSE>
    $<$<CONFIG:Release>:TRUE>
    $<$<CONFIG:MinSizeRel>:TRUE>)

project(Diopter LANGUAGES CXX)

set(SOURCES
    "src/platform/window.h"
    "src/platform/window.cpp"
    "src/platform/imgui_impl_sdl2.h"
    "src/platform/imgui_impl_sdl2.cpp"
    "src/gui/dbg_gui.h"
    "src/gui/dbg_gui.cpp"
    "src/gui/imgui_ext.h"
    "src/scene/pbrt.h"
    "src/scene/pbrt.cpp"
    "src/scene/gltf.h"
    "src/scene/gltf.cpp"
    "src/scene/rply.h"
    "src/scene/rply.cpp"
    "src/scene/gpu_scene.h"
    "src/scene/gpu_scene.cpp"
    "src/scene/encode.cpp"
    "src/scene/encode.h"
    "src/main.cpp"
    "src/diopter.h"
    "src/diopter.cpp"
    "src/util/image.h"
    "src/util/camera.h"
    "src/util/camera.cpp"
    "src/renderer/renderer.cpp"
    "src/renderer/renderer.h"
    "src/renderer/ao.cpp"
    "src/renderer/ao.h"
    "src/renderer/matpath.cpp"
    "src/renderer/matpath.h"
    "src/renderer/geometry.cpp"
    "src/renderer/geometry.h"
    "src/renderer/shading.cpp"
    "src/renderer/shading.h"
    "src/renderer/post.cpp"
    "src/renderer/post.h"
    "src/renderer/pipeline.h"
)

if(RELEASE_BUILD)
    set(SOURCES ${SOURCES} "src/shaders/precompiled.h" "src/shaders/precompiled.cpp")
endif()

if(MSVC)
    add_compile_options(/arch:AVX2)
else()
    add_compile_options(-mavx2)
endif()

add_executable(Diopter ${SOURCES})
set_target_properties(Diopter PROPERTIES CXX_STANDARD 20 CXX_EXTENSIONS OFF)

if(MSVC)
    set_target_properties(Diopter PROPERTIES VS_STARTUP_PROJECT Diopter
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data")

    target_compile_definitions(Diopter
        PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _HAS_EXCEPTIONS=0
        $<$<CONFIG:Debug>:>
        $<$<CONFIG:RelWithDebInfo>:>
        $<$<CONFIG:Release>:_NO_DEBUG_HEAP=1>
        $<$<CONFIG:MinSizeRel>:_NO_DEBUG_HEAP=1>
    )
    target_compile_options(Diopter PRIVATE /MP /W4 /GR- /GT /GS- /EHa- /wd4201)
else()
    target_compile_options(Diopter PRIVATE -mf16c -Wall -Wextra -fno-rtti -Wno-reorder-ctor -Wno-missing-field-initializers)
endif()

set_property(TARGET Diopter PROPERTY INTERPROCEDURAL_OPTIMIZATION RELEASE_BUILD)

set(RVK_HAS_RPP TRUE)
set(RVK_NV_AFTERMATH TRUE)

if(RELEASE_BUILD OR LINUX)
    set(RVK_NV_AFTERMATH FALSE)
endif()

add_subdirectory("deps/rpp/")
add_subdirectory("deps/rvk/")

add_subdirectory("deps/tinyexr")
add_subdirectory("deps/tinygltf")
add_subdirectory("deps/rply/")
add_subdirectory("deps/stb/")
add_subdirectory("deps/nfd/")

target_include_directories(Diopter PRIVATE "deps/" ${RPP_INCLUDE_DIRS} ${RVK_INCLUDE_DIRS})
target_link_libraries(Diopter PRIVATE rvk rpp rply nfd stb tinygltf tinyexr)

if(WIN32)
    set_target_properties(Diopter PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} \"${CMAKE_CURRENT_SOURCE_DIR}/data/icon/icon.res\"")
    target_include_directories(Diopter PRIVATE "deps/sdl2")
    target_link_options(rpp PRIVATE /NODEFAULTLIB:MSVCRT)

    target_link_libraries(Diopter PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/deps/sdl2/SDL2main.lib")
    target_link_libraries(Diopter PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/deps/sdl2/SDL2-static.lib")
    target_link_libraries(Diopter PRIVATE Winmm Version Setupapi)
elseif(LINUX)
    find_package(SDL2 REQUIRED)
    target_include_directories(Diopter PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(Diopter PRIVATE ${SDL2_LIBRARIES})
endif()

function(add_shader TARGET SHADER DEPS)
    find_program(DXC dxc)
    set(dep_paths)

    foreach(DEP IN LISTS DEPS)
        list(APPEND dep_paths ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/${DEP})
    endforeach()

    get_filename_component(SHADER_NAME ${SHADER} NAME_WLE)
    get_filename_component(SHADER_DIR ${SHADER} DIRECTORY)
    set(CURRENT-SHADER-PATH ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/${SHADER})
    set(CURRENT-OUTPUT-PATH ${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/${SHADR_DIR}${SHADER_NAME}.spv)
    get_filename_component(CURRENT-OUTPUT-DIR ${CURRENT-OUTPUT-PATH} DIRECTORY)
    file(MAKE_DIRECTORY ${CURRENT-OUTPUT-DIR})

    if(${ARGC} GREATER 3)
        set(TYPE ${ARGV3})
    else()
        set(TYPE lib)
    endif()

    add_custom_command(
        OUTPUT ${CURRENT-OUTPUT-PATH}
        COMMAND ${DXC}
        -spirv
        -enable-16bit-types
        -fvk-use-scalar-layout
        -fspv-target-env=vulkan1.3
        -T ${TYPE}_6_6
        -E main
        -Zi
        -O2
        -Fo ${CURRENT-OUTPUT-PATH}
        ${CURRENT-SHADER-PATH}
        DEPENDS ${CURRENT-SHADER-PATH} ${dep_paths}
        VERBATIM)
    set_source_files_properties(${CURRENT-OUTPUT-PATH} PROPERTIES GENERATED TRUE)
    target_sources(${TARGET} PRIVATE ${CURRENT-OUTPUT-PATH})
endfunction(add_shader)

add_custom_target(Shaders ALL)
add_dependencies(Diopter Shaders)

file(GLOB HLSL_MATERIALS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/materials/*.hlsl)
list(JOIN HLSL_MATERIALS ";" HLSL_MATERIALS)
set(HLSL_LIB "util.hlsl;khr_builtins.hlsl;hit_info.hlsl;scene.hlsl;alpha_mask.hlsl;${HLSL_MATERIALS}")

add_shader(Shaders "post/post.hlsl" "${HLSL_LIB}" cs)

add_shader(Shaders "matpath/mp.rg.hlsl" "matpath/mp.hlsl;${HLSL_LIB}")
add_shader(Shaders "matpath/mp.rm.hlsl" "matpath/mp.hlsl;${HLSL_LIB}")
add_shader(Shaders "matpath/mp.rh.hlsl" "matpath/mp.hlsl;${HLSL_LIB}")
add_shader(Shaders "matpath/mp.ra.hlsl" "matpath/mp.hlsl;${HLSL_LIB}")

add_shader(Shaders "ao/ao.rg.hlsl" "ao/ao.hlsl;${HLSL_LIB}")
add_shader(Shaders "ao/ao.rm.hlsl" "ao/ao.hlsl;${HLSL_LIB}")
add_shader(Shaders "ao/ao.rh.hlsl" "ao/ao.hlsl;${HLSL_LIB}")
add_shader(Shaders "ao/ao.ra.hlsl" "ao/ao.hlsl;${HLSL_LIB}")

add_shader(Shaders "geometry/geometry.rg.hlsl" "geometry/geometry.hlsl;${HLSL_LIB}")
add_shader(Shaders "geometry/geometry.rm.hlsl" "geometry/geometry.hlsl;${HLSL_LIB}")
add_shader(Shaders "geometry/geometry.rh.hlsl" "geometry/geometry.hlsl;${HLSL_LIB}")
add_shader(Shaders "geometry/geometry.ra.hlsl" "geometry/geometry.hlsl;${HLSL_LIB}")

add_shader(Shaders "shading/shading.rg.hlsl" "shading/shading.hlsl;${HLSL_LIB}")
add_shader(Shaders "shading/shading.rm.hlsl" "shading/shading.hlsl;${HLSL_LIB}")
add_shader(Shaders "shading/shading.rh.hlsl" "shading/shading.hlsl;${HLSL_LIB}")
