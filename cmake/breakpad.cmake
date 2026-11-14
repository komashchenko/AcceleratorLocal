include(ExternalProject)

set(BREAKPAD_DIR "${PROJECT_SOURCE_DIR}/third_party/breakpad" CACHE PATH "Breakpad checkout")

function(apply_breakpad_patches breakpad_dir)
	find_package(Git REQUIRED)

	file(GLOB patch_files CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/*.patch"
	)
	list(SORT patch_files)
	if(NOT patch_files)
		return()
	endif()

	set(reverse_patch_files ${patch_files})
	list(REVERSE reverse_patch_files)
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${breakpad_dir}"
			-C "${breakpad_dir}" apply --reverse --check ${reverse_patch_files}
		RESULT_VARIABLE reverse_result
		OUTPUT_QUIET ERROR_QUIET
	)
	if(reverse_result EQUAL 0)
		return()
	endif()

	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${breakpad_dir}"
			-C "${breakpad_dir}" apply --check ${patch_files}
		RESULT_VARIABLE check_result
		ERROR_VARIABLE patch_error
	)
	if(NOT check_result EQUAL 0)
		message(FATAL_ERROR
			"Cannot apply Breakpad patches. Remove previously applied patches or use a clean checkout of the pinned revision.\n${patch_error}"
		)
	endif()

	message(STATUS "Applying Breakpad patches")
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${breakpad_dir}"
			-C "${breakpad_dir}" apply ${patch_files}
		COMMAND_ERROR_IS_FATAL ANY
	)
endfunction()

function(add_breakpad_target target_name breakpad_dir)
	apply_breakpad_patches("${breakpad_dir}")
	find_program(MAKE_EXECUTABLE NAMES gmake make REQUIRED)
	find_package(Threads REQUIRED)
	find_package(ZLIB REQUIRED)

	set(autotools_prefix "${CMAKE_BINARY_DIR}/${target_name}-autotools")
	set(autotools_build_dir "${autotools_prefix}/build")
	set(processor_library "${autotools_build_dir}/src/libbreakpad.a")
	set(client_library "${autotools_build_dir}/src/client/linux/libbreakpad_client.a")
	set(disasm_library "${autotools_build_dir}/src/third_party/libdisasm/libdisasm.a")

	string(TOUPPER "${CMAKE_BUILD_TYPE}" build_type_upper)
	set(breakpad_c_flags "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${build_type_upper}}")
	set(breakpad_cxx_flags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${build_type_upper}}")
	set(breakpad_cpp_flags "-DBPLOG_MINIMUM_SEVERITY=SEVERITY_ERROR -D__STDC_FORMAT_MACROS -D_GLIBCXX_USE_CXX11_ABI=0")
	if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		string(APPEND breakpad_cxx_flags " -fno-gnu-unique")
	endif()
	string(STRIP "${breakpad_c_flags}" breakpad_c_flags)
	string(STRIP "${breakpad_cxx_flags}" breakpad_cxx_flags)

	set(autotools_target "${target_name}_autotools")
	ExternalProject_Add(${autotools_target}
		PREFIX "${autotools_prefix}"
		SOURCE_DIR "${breakpad_dir}"
		BINARY_DIR "${autotools_build_dir}"
		DOWNLOAD_COMMAND ""
		UPDATE_COMMAND ""
		PATCH_COMMAND ""
		CONFIGURE_COMMAND
			"${CMAKE_COMMAND}" -E env
			"CC=${CMAKE_C_COMPILER}"
			"CXX=${CMAKE_CXX_COMPILER}"
			"CFLAGS=${breakpad_c_flags}"
			"CXXFLAGS=${breakpad_cxx_flags}"
			"CPPFLAGS=${breakpad_cpp_flags}"
			"<SOURCE_DIR>/configure" --disable-tools
		BUILD_COMMAND
			"${MAKE_EXECUTABLE}" -j4
				src/libbreakpad.a
				src/client/linux/libbreakpad_client.a
				src/third_party/libdisasm/libdisasm.a
		BUILD_ALWAYS TRUE
		INSTALL_COMMAND ""
		BUILD_BYPRODUCTS
			"${processor_library}"
			"${client_library}"
			"${disasm_library}"
		USES_TERMINAL_CONFIGURE TRUE
		USES_TERMINAL_BUILD TRUE
	)

	set(src "${breakpad_dir}/src")

	add_library(${target_name}_processor STATIC IMPORTED GLOBAL)
	set_target_properties(${target_name}_processor PROPERTIES
		IMPORTED_LOCATION "${processor_library}"
		INTERFACE_INCLUDE_DIRECTORIES "${src}"
	)
	add_dependencies(${target_name}_processor ${autotools_target})

	add_library(${target_name}_client STATIC IMPORTED GLOBAL)
	set_target_properties(${target_name}_client PROPERTIES
		IMPORTED_LOCATION "${client_library}"
		INTERFACE_INCLUDE_DIRECTORIES "${src}"
	)
	add_dependencies(${target_name}_client ${autotools_target})

	add_library(${target_name}_disasm STATIC IMPORTED GLOBAL)
	set_target_properties(${target_name}_disasm PROPERTIES
		IMPORTED_LOCATION "${disasm_library}"
	)
	add_dependencies(${target_name}_disasm ${autotools_target})

	# Upstream exposes these sources only through the dump_syms executable.
	add_library(${target_name}_symbols STATIC
		${src}/common/dwarf_cfi_to_module.cc
		${src}/common/dwarf_cu_to_module.cc
		${src}/common/dwarf_line_to_module.cc
		${src}/common/dwarf_range_list_handler.cc
		${src}/common/language.cc
		${src}/common/module.cc
		${src}/common/path_helper.cc
		${src}/common/stabs_reader.cc
		${src}/common/stabs_to_module.cc
		${src}/common/dwarf/bytereader.cc
		${src}/common/dwarf/dwarf2diehandler.cc
		${src}/common/dwarf/dwarf2reader.cc
		${src}/common/dwarf/elf_reader.cc
		${src}/common/linux/crc32.cc
		${src}/common/linux/dump_symbols.cc
		${src}/common/linux/elf_symbols_to_module.cc
		${src}/common/linux/elfutils.cc
		${src}/common/linux/file_id.cc
		${src}/common/linux/linux_libc_support.cc
		${src}/common/linux/memory_mapped_file.cc
		${src}/common/linux/safe_readlink.cc
	)
	add_dependencies(${target_name}_symbols ${autotools_target})
	set_target_properties(${target_name}_symbols PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
		POSITION_INDEPENDENT_CODE ON
	)
	target_compile_definitions(${target_name}_symbols PRIVATE
		BPLOG_MINIMUM_SEVERITY=SEVERITY_ERROR
		HAVE_CONFIG_H=1
		__STDC_FORMAT_MACROS
	)
	target_compile_options(${target_name}_symbols PRIVATE
		-Wno-deprecated-declarations
		-Wno-sign-compare
		-Wno-unused-parameter
	)
	target_include_directories(${target_name}_symbols SYSTEM PUBLIC
		"${src}"
		"${autotools_build_dir}/src"
	)
	target_link_libraries(${target_name}_symbols PUBLIC ZLIB::ZLIB)

	add_library(${target_name} INTERFACE)
	add_library(breakpad::breakpad ALIAS ${target_name})
	target_compile_definitions(${target_name} INTERFACE
		BPLOG_MINIMUM_SEVERITY=SEVERITY_ERROR
	)
	target_link_libraries(${target_name} INTERFACE
		${target_name}_symbols
		${target_name}_client
		${target_name}_processor
		${target_name}_disasm
		Threads::Threads
		ZLIB::ZLIB
		${CMAKE_DL_LIBS}
	)
endfunction()
