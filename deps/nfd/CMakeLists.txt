cmake_minimum_required(VERSION 3.17)

if(UNIX AND NOT APPLE)
	set(LINUX TRUE)
endif()

if(APPLE)
	project(nfd LANGUAGES C OBJC)

	set(SOURCES_NFD "nfd_common.c"
		"nfd_common.h"
		"nfd_cocoa.m")

	add_library(nfd STATIC ${SOURCES_NFD})
elseif(LINUX)
	project(nfd LANGUAGES C)

	set(SOURCES_NFD "nfd_common.c"
		"nfd_common.h"
		"nfd_gtk.c")

	add_library(nfd STATIC ${SOURCES_NFD})

	find_package(PkgConfig REQUIRED)
	pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
	target_include_directories(nfd PRIVATE ${GTK3_INCLUDE_DIRS})
	target_link_directories(nfd PRIVATE ${GTK3_LIBRARY_DIRS})
	target_compile_options(nfd PRIVATE ${GTK3_CFLAGS_OTHER})
	target_link_libraries(nfd PRIVATE ${GTK3_LIBRARIES})
elseif(WIN32)
	project(nfd LANGUAGES C CXX)

	set(SOURCES_NFD "nfd_common.c"
		"nfd_common.h"
		"nfd_win.cpp")

	add_library(nfd STATIC ${SOURCES_NFD})
else()
	message(FATAL_ERROR "Unsupported platform: only Windows, Linux, and macOS are supported.")
endif()

if(MSVC)
	target_compile_definitions(nfd
		PRIVATE
		WIN32_LEAN_AND_MEAN NOMINMAX _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS
		$<$<CONFIG:Debug>:>
		$<$<CONFIG:RelWithDebInfo>:>
		$<$<CONFIG:Release>:_NO_DEBUG_HEAP=1>
		$<$<CONFIG:MinSizeRel>:_NO_DEBUG_HEAP=1>
	)
	target_compile_options(nfd PRIVATE /MP /W4 /GR- /GS- /EHa- /wd4201)
else()
	if(LINUX)
		target_compile_options(nfd PRIVATE -Wall -Wextra)
	else()
		target_compile_options(nfd PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
	endif()
endif()

set_target_properties(rply PROPERTIES CXX_STANDARD 20 CXX_EXTENSIONS OFF)
set_target_properties(rply PROPERTIES OBJC_STANDARD 11 OBJC_EXTENSIONS OFF)
set_target_properties(rply PROPERTIES C_STANDARD 17 C_EXTENSIONS OFF)
