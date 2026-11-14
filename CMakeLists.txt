cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

project(AcceleratorLocal
	VERSION 1.1.0
	DESCRIPTION "Local crash handler for Counter-Strike 2"
	HOMEPAGE_URL "https://github.com/komashchenko/AcceleratorLocal"
	LANGUAGES C CXX
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
	message(FATAL_ERROR "AcceleratorLocal supports Linux only")
endif()

add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)

set(CMAKE_SKIP_RPATH ON)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-gnu-unique>)
endif()

include(cmake/sourcesdk.cmake)
include(cmake/metamod.cmake)
include(cmake/breakpad.cmake)
add_breakpad_target(breakpad "${BREAKPAD_DIR}")

add_library(AcceleratorLocal SHARED
	src/acceleratorlocal.cpp
	src/acceleratorlocal.h
	src/config.cpp
	src/config.h
	src/crashhandler.cpp
	src/crashhandler.h
	src/minidumpprocessor.cpp
	src/minidumpprocessor.h
)

set_target_properties(AcceleratorLocal PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
	CXX_VISIBILITY_PRESET hidden
	PREFIX ""
	VISIBILITY_INLINES_HIDDEN ON
)

target_compile_definitions(AcceleratorLocal PRIVATE
	ACCELERATORLOCAL_VERSION="${PROJECT_VERSION}"
	_FILE_OFFSET_BITS=64
	__STDC_FORMAT_MACROS
)

target_compile_options(AcceleratorLocal PRIVATE
	-Wall
	-Wextra
	-Wno-unused-parameter
	-fno-exceptions
	-fno-omit-frame-pointer
	-fno-strict-aliasing
	-fno-threadsafe-statics
)

target_link_libraries(AcceleratorLocal PRIVATE
	breakpad::breakpad
	sourcesdk::sourcesdk
	acceleratorlocal_metamod
)

target_link_options(AcceleratorLocal PRIVATE
	-static-libstdc++
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	target_link_libraries(AcceleratorLocal PRIVATE gcc_eh)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_link_options(AcceleratorLocal PRIVATE -static-libgcc)
endif()

include(cmake/package.cmake)
