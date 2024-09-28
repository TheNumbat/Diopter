cmake_minimum_required(VERSION 3.17)

project(tinygltf LANGUAGES CXX)

set(SOURCES_TINYGLTF
    "tiny_gltf.cpp"
    "tiny_gltf.h"
    "json.hpp"
)

add_library(tinygltf STATIC ${SOURCES_TINYGLTF})

target_include_directories(tinygltf PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../stb")

if(MSVC)
    target_compile_definitions(tinygltf
        PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS
        $<$<CONFIG:Debug>:>
        $<$<CONFIG:RelWithDebInfo>:>
        $<$<CONFIG:Release>:_NO_DEBUG_HEAP=1>
        $<$<CONFIG:MinSizeRel>:_NO_DEBUG_HEAP=1>
    )
    target_compile_options(tinygltf PRIVATE /MP /W4 /GR- /GS- /EHa- /wd4018 /wd4267)
else()
    target_compile_options(tinygltf PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

set_target_properties(tinygltf PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF)
