#-----------------------------------------------------------------------------
#
# Super Mario War project configuration file
#
#-----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.25)

project(smw
	VERSION 2.0.0
	DESCRIPTION "A fan-made multiplayer Super Mario Bros. style deathmatch game"
	HOMEPAGE_URL "https://github.com/mmatyas/supermariowar"
	LANGUAGES CXX
)

# Original Xbox, built with RXDK (-DCMAKE_TOOLCHAIN_FILE=<RXDK tools>/cmake/RxdkToolchain.cmake).
# SDL comes from the compatibility layer in platform/xbox (it has some C sources).
# The netplay runs over system link (ENet on the Xbox's Winsock, platform/xbox).
# The editors are built into the game (src/smw/editors); no unit tests and no
# standalone editors on the console.
if(XBOXOG)
	enable_language(C)
	set(BUILD_TESTS OFF CACHE BOOL "Build the unit tests" FORCE)
	set(SMW_INSTALL_PORTABLE ON CACHE BOOL "Install to a single directory" FORCE)
	# Float maths on SSE, like the PC builds: exact single precision, so the
	# simulation gives the same results every time (replays, rollback). The x87
	# keeps intermediate results in 80 bits, rounded differently depending on how
	# the compiler kept them in registers. (Doubles stay on the x87, the Pentium
	# III has no SSE2; the SDL layer sets it to 53-bit precision.)
	add_compile_options(-mfpmath=sse)
endif()

#
# Options for customizing builds
#

option(USE_PNG_SAVE "Use PNG for screenshots and thumbnails" ON)
option(NO_NETWORK "Disable all network communication" OFF)
option(SDL2_FORCE_GLES "(SDL2) Use OpenGL ES rendering if possible" OFF)
option(BUILD_TESTS "Build the unit tests" ON)

set(SMW_INSTALL_PORTABLE_DEFAULT OFF)
if(WIN32)
	set(SMW_INSTALL_PORTABLE_DEFAULT ON)
endif()
option(SMW_INSTALL_PORTABLE "Install to a single directory" ${SMW_INSTALL_PORTABLE_DEFAULT})

# Additional CMake search path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

# Visual Studio, Xcode and "Ninja Multi-Config" pick the configuration at build
# time, so CMAKE_BUILD_TYPE only applies to single-configuration generators.
get_property(SMW_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if (NOT SMW_IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING
		"Build type (Release/Debug/RelWithDebInfo/MinSizeRel)" FORCE)
endif()

# Set where the binary files will be built.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(SmwGitInfo)

#-----------------------------------------------------------------------------
#
# Platform-specific settings
#
#-----------------------------------------------------------------------------

if (EMSCRIPTEN)
	include(PlatformEmscripten)
endif()

#-----------------------------------------------------------------------------
#
# Dependencies
#
#-----------------------------------------------------------------------------

# Don't create the CTest dashboard targets (Continuous, Experimental, Nightly...)
# here or in the dependencies; they would only clutter IDE project lists
set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)

include(BundledDeps)

#-----------------------------------------------------------------------------
#
# Compiler flags
#
#-----------------------------------------------------------------------------

# Debug/release defines. Generator expressions keep these correct for
# multi-configuration generators too (e.g. the Debug config of a VS solution).
add_compile_definitions(
	"$<$<CONFIG:Debug>:DEBUG>"
	"$<$<CONFIG:Debug>:_DEBUG>"
	"$<$<CONFIG:Debug>:PNG_DEBUG=1>"
	"$<$<NOT:$<CONFIG:Debug>>:NDEBUG>"
)

# Optimization flags for single-configuration GCC/Clang builds.
# MSVC and multi-configuration generators use CMake's per-configuration
# defaults (CMAKE_<LANG>_FLAGS_<CONFIG>), e.g. /Od for Debug and /O2 for Release.
# (RXDK builds take the optimization flags of each configuration from the toolchain.)
if (NOT MSVC AND NOT SMW_IS_MULTI_CONFIG AND NOT XBOXOG)
	if (CMAKE_BUILD_TYPE STREQUAL Debug)
		# Turn optimizations off; turn debugging symbols on.
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -g -O0")
	else()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -O3")

		# Strip binaries
		if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s")
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s")
		endif()
	endif()
endif()

if (MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
	# Compile the files of each project in parallel inside Visual Studio
	add_compile_options(/MP)
endif()

if (EMSCRIPTEN)
	if (CMAKE_BUILD_TYPE STREQUAL Debug)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g4")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g4")
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0 -g4")
	else()
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O3")
	endif()
endif()

include(CheckCXXCompilerFlag)
macro(check_and_add_flag var flag)
	CHECK_CXX_COMPILER_FLAG(${flag} FLAG_${var})
	if(FLAG_${var})
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${flag}")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flag}")
	endif()
endmacro()

# Enabling all warnings in MSVC spams too much
if(NOT MSVC)
	# TODO: would like these but they produce overwhelming amounts of warnings
	check_and_add_flag(ALL -Wall)
	check_and_add_flag(EXTRA -Wextra)
	check_and_add_flag(CXX11_NARROWING_OFF -Wno-c++11-narrowing)
	check_and_add_flag(OVERLOADED_VIRTUAL_OFF -Wno-overloaded-virtual)
	check_and_add_flag(UNUSED_PARAMETER_OFF -Wno-unused-parameter)
	check_and_add_flag(ALWAYS_RETURN -Werror=return-type)

	# gcc uses some optimizations which might break stuff without this flag
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-strict-aliasing -fexceptions -Wno-shadow")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-strict-aliasing -fexceptions -Wno-shadow")

endif(NOT MSVC)

if (APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-c++11-narrowing")
endif()

## MAIN CONFIG

check_and_add_flag(VISIBILITY_INLINES_HIDDEN -fvisibility-inlines-hidden)

if(UNIX AND NOT APPLE)
	check_and_add_flag(VISIBILITY_HIDDEN -fvisibility=hidden)
endif()

#-----------------------------------------------------------------------------
#
# Installation locations
#
#-----------------------------------------------------------------------------

# Common install locations, also passed to the game components

if(WIN32 OR SMW_INSTALL_PORTABLE)
	# CMAKE_INSTALL_PREFIX defaults to `C:/Program Files/smw`
	set(SMW_BINDIR  .    CACHE PATH "Installation location of the game binaries")
	set(SMW_DATADIR data CACHE PATH "Installation location of the game data")
	set(SMW_DOCDIR  .    CACHE PATH "Installation location of the game documentation")
else()
	include(GNUInstallDirs)
	# CMAKE_INSTALL_PREFIX defaults to `/usr/local`
	set(SMW_BINDIR "${CMAKE_INSTALL_PREFIX}/games" CACHE PATH "Installation location of the game binaries")
	set(SMW_DATADIR "${CMAKE_INSTALL_FULL_DATADIR}/games/smw" CACHE PATH "Installation location of the game data")
	set(SMW_DOCDIR "${CMAKE_INSTALL_FULL_DOCDIR}" CACHE PATH "Installation location of the game documentation")
	set(SMW_DESKTOPDIR "${CMAKE_INSTALL_FULL_DATADIR}/applications" CACHE PATH "Installation location of the desktop launchers")
	set(SMW_PIXMAPDIR "${CMAKE_INSTALL_FULL_DATADIR}/pixmaps" CACHE PATH "Installation location of the desktop pixmaps")
	set(SMW_ICONDIR "${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor" CACHE PATH "Installation location of the desktop icons")
endif()

#
# Data directory install location
#

install(DIRECTORY data/
	DESTINATION "${SMW_DATADIR}"
	PATTERN .git EXCLUDE
	PATTERN ZZleveleditor.map EXCLUDE
)
install(FILES docs/readme-v1.8.html DESTINATION "${SMW_DOCDIR}")

#-----------------------------------------------------------------------------
#
# Ready to build!
#
#-----------------------------------------------------------------------------

# Dump some information
message("    SYSTEM:      ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
message("    BUILD:       ${CMAKE_BUILD_TYPE}")
message("    CC:          ${CMAKE_C_COMPILER}")
message("    CXX:         ${CMAKE_CXX_COMPILER}")
message("    C FLAGS:     ${CMAKE_C_FLAGS}")
message("    CXX FLAGS:   ${CMAKE_CXX_FLAGS}")
message("    LD FLAGS:    ${CMAKE_EXE_LINKER_FLAGS}")

get_directory_property(MSG_DEFINITIONS COMPILE_DEFINITIONS)
message("    DEFINITIONS: ${MSG_DEFINITIONS}")

message("  [install]")
message("    Binary dir:  ${SMW_BINDIR}")
message("    Data dir:    ${SMW_DATADIR}")
message("    Docs dir:    ${SMW_DOCDIR}")

#
# Go through every sub-project
#

include_directories(src/common)
include_directories(src/smw)

add_subdirectory(src/common)
add_subdirectory(src/smw)
if(NOT XBOXOG)
	add_subdirectory(src/leveleditor)
	add_subdirectory(src/worldeditor)
endif()

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTS)
	include(CTest)
	include(dependencies/doctest/doctest.cmake)
	add_subdirectory(dependencies/doctest)
	add_subdirectory(tests)
endif()

# Visual Studio: start the game when pressing F5, and run the game and the
# editors against the repository's data folder
if (CMAKE_GENERATOR MATCHES "Visual Studio")
	set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT smw)
	foreach(smw_target IN ITEMS smw smw-leveledit smw-worldedit)
		set_target_properties(${smw_target} PROPERTIES
			VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
			VS_DEBUGGER_COMMAND_ARGUMENTS "--datadir \"${PROJECT_SOURCE_DIR}/data\""
		)
	endforeach()
endif()

# Copy the game data next to the executables, so the built game and editors
# also run when started directly (e.g. by double-clicking smw.exe in
# build/vs/Debug). Only changed files are copied on later builds.
set(SMW_COPY_DATA_DEFAULT OFF)
if (CMAKE_GENERATOR MATCHES "Visual Studio")
	set(SMW_COPY_DATA_DEFAULT ON)
endif()
option(SMW_COPY_DATA_TO_BUILD "Copy the data folder next to the built executables" ${SMW_COPY_DATA_DEFAULT})

if (SMW_COPY_DATA_TO_BUILD)
	if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
		set(smw_copy_dir_command copy_directory_if_different)
	else()
		set(smw_copy_dir_command copy_directory)
	endif()
	add_custom_target(copy-data
		COMMAND ${CMAKE_COMMAND} -E ${smw_copy_dir_command} "${PROJECT_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:smw>/data"
		COMMENT "Copying game data next to the executables"
		VERBATIM
	)
	set_property(TARGET copy-data PROPERTY FOLDER "Utility")
	foreach(smw_target IN ITEMS smw smw-leveledit smw-worldedit)
		add_dependencies(${smw_target} copy-data)
	endforeach()
endif()

# IDE folders (Visual Studio, Xcode): keep the game's own projects at the top
# level and collect the bundled third-party libraries in a "Dependencies" folder
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

function(smw_set_folder_recursive dir folder)
	get_property(dir_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
	foreach(dir_target IN LISTS dir_targets)
		set_property(TARGET ${dir_target} PROPERTY FOLDER "${folder}")
	endforeach()
	get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
	foreach(subdir IN LISTS subdirs)
		smw_set_folder_recursive("${subdir}" "${folder}")
	endforeach()
endfunction()

set(smw_src_dir "${PROJECT_SOURCE_DIR}/src")
set(smw_tests_dir "${PROJECT_SOURCE_DIR}/tests")
get_property(smw_top_subdirs DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY SUBDIRECTORIES)
foreach(subdir IN LISTS smw_top_subdirs)
	# Only src/ and tests/ hold the game's own projects; everything else
	# (FetchContent checkouts under _deps, doctest) is third-party code
	cmake_path(IS_PREFIX smw_src_dir "${subdir}" NORMALIZE is_src)
	cmake_path(IS_PREFIX smw_tests_dir "${subdir}" NORMALIZE is_tests)
	if (NOT is_src AND NOT is_tests)
		smw_set_folder_recursive("${subdir}" "Dependencies")
	endif()
endforeach()
foreach(smw_lib IN ITEMS CommonFiles)
	if (TARGET ${smw_lib})
		set_property(TARGET ${smw_lib} PROPERTY FOLDER "Libraries")
	endif()
endforeach()

#-----------------------------------------------------------------------------
#
# Packaging
#
#-----------------------------------------------------------------------------

#
# Common settings
#

set(CPACK_PACKAGE_NAME "smw")
set(CPACK_PACKAGE_VENDOR "72dpiarmy")
set(CPACK_PACKAGE_VERSION_MAJOR ${SMW_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${SMW_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${SMW_VERSION_PATCH})
set(CPACK_PACKAGE_DESCRIPTION_FILE ${PROJECT_SOURCE_DIR}/resources/package_description.txt)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Super Mario War, arcade platform multiplayer game")

set(CPACK_SET_DESTDIR ON)
set(CPACK_SOURCE_IGNORE_FILES  "\\\\.#;/#;.*~;\\\\.swp;/\\\\.git")
list(APPEND CPACK_SOURCE_IGNORE_FILES "${CMAKE_BINARY_DIR}")

set(CPACK_GENERATOR "TGZ;DEB;RPM")
set(CPACK_SOURCE_GENERATOR "TGZ;TBZ2;ZIP")

#
# Debian
#
set(CPACK_DEBIAN_PACKAGE_SECTION Games)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "nobody")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsdl2-2.0-0, libsdl2-mixer-2.0-0, libsdl2-image-2.0-0")
if (NOT BUILD_STATIC_LIBS)
	if(NOT NO_NETWORK)
		set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libenet2a")
	endif()
endif()

#
# RPM
#
set(CPACK_RPM_PACKAGE_GROUP Amusements/Games)
set(CPACK_RPM_PACKAGE_LICENSE GPL-2.0)
set(CPACK_RPM_PACKAGE_REQUIRES "SDL2, SDL2_image, SDL2_mixer")
if (NOT BUILD_STATIC_LIBS)
	if(NOT NO_NETWORK)
		set(CPACK_RPM_PACKAGE_REQUIRES "${CPACK_RPM_PACKAGE_REQUIRES}, enet")
	endif()
endif()

#
# OSX, Windows: TODO
#
set(CPACK_RESOURCE_FILE_README ${PROJECT_SOURCE_DIR}/README.md)
# TODO: CPACK_RESOURCE_FILE_WELCOME
set(CPACK_PACKAGE_ICON ${PROJECT_SOURCE_DIR}/src/resources/smw.ico)
# TODO: CPACK_NSIS_*

# CPack must be included after the CPACK_* variables are set in order for those
# variables to take effect.
include(CPack)
