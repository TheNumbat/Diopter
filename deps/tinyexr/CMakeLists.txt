cmake_minimum_required(VERSION 3.17)

project(tinyexr LANGUAGES C CXX)

set(SOURCES_TINYEXR
    "tinyexr.cc"
    "tinyexr.h"
)

add_library(tinyexr STATIC ${SOURCES_TINYEXR})

target_compile_definitions(tinyexr PRIVATE TINYEXR_USE_MINIZ=0)
target_compile_definitions(tinyexr PRIVATE TINYEXR_USE_STB_ZLIB=1)

if(MSVC)
    target_compile_definitions(tinyexr
        PRIVATE
        NOMINMAX _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS
        $<$<CONFIG:Debug>:>
        $<$<CONFIG:RelWithDebInfo>:>
        $<$<CONFIG:Release>:_NO_DEBUG_HEAP=1>
        $<$<CONFIG:MinSizeRel>:_NO_DEBUG_HEAP=1>
    )
    target_compile_options(tinyexr PRIVATE /MP /W4 /GR- /GS- /EHa- /wd4018 /wd4267 /wd4245 /wd4706)
else()
    target_compile_options(tinyexr PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

set_target_properties(tinyexr PROPERTIES C_STANDARD 17 CXX_STANDARD 17 C_EXTENSIONS OFF CXX_EXTENSIONS OFF)
