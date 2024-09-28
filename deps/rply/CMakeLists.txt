cmake_minimum_required(VERSION 3.17)

project(rply LANGUAGES C)

set(SOURCES_RPLY
    "rply.c"
    "rply.h"
    "rplyfile.h")

add_library(rply STATIC ${SOURCES_RPLY})

if(MSVC)
    target_compile_definitions(rply
        PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS
        $<$<CONFIG:Debug>:>
        $<$<CONFIG:RelWithDebInfo>:>
        $<$<CONFIG:Release>:_NO_DEBUG_HEAP=1>
        $<$<CONFIG:MinSizeRel>:_NO_DEBUG_HEAP=1>
    )
    target_compile_options(rply PRIVATE /MP /W4 /GR- /GS- /EHa- /wd4201)
else()
    target_compile_options(rply PRIVATE -Wall -Wextra)
endif()

set_target_properties(rply PROPERTIES C_STANDARD 17 C_EXTENSIONS OFF)
