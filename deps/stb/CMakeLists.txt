cmake_minimum_required(VERSION 3.17)

project(stb LANGUAGES C)

set(SOURCES_STB
    "stb_image_write.h"
    "stb_image.h"
    "stb_perlin.h"
    "stb.cpp"
)

add_library(stb STATIC ${SOURCES_STB})

if(MSVC)
    target_compile_definitions(stb
        PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS
        $<$<CONFIG:Debug>:>
        $<$<CONFIG:RelWithDebInfo>:>
        $<$<CONFIG:Release>:_NO_DEBUG_HEAP=1>
        $<$<CONFIG:MinSizeRel>:_NO_DEBUG_HEAP=1>
    )
    target_compile_options(stb PRIVATE /MP /W4 /GR- /GS- /EHa- /wd4201)
else()
    target_compile_options(stb PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

set_target_properties(stb PROPERTIES C_STANDARD 17 C_EXTENSIONS OFF)
