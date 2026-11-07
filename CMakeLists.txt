# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

cmake_minimum_required(VERSION 4.2 FATAL_ERROR)

project(libspectrum VERSION 1.8.0 LANGUAGES C)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# configure.ac also requires C23 project-wide (checked via AC_COMPILE_IFELSE).
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# CMake's compiler-standard machinery doesn't map C_STANDARD 23 to a
# recognised flag on every MSVC toolset (some only know /std:clatest, not
# /std:c23), and MSVC additionally gates <stdatomic.h> behind
# /experimental:c11atomics regardless of the C standard selected.
set(LIBSPECTRUM_C23_FLAGS "")
if(MSVC)
    set(LIBSPECTRUM_C23_FLAGS /std:clatest /experimental:c11atomics)
endif()

option(BUILD_SHARED_LIBS "Build libspectrum shared library" ON)
option(ZLIB_SUPPORT "Use zlib for compressed file support" ON)
option(BZIP2_SUPPORT "Use bzip2 for compressed file support" ON)
option(WAV_SUPPORT "Enable WAV audio files" ON)
option(LIBGCRYPT_SUPPORT "Use libgcrypt for RZX signing/verification" ON)
option(USE_FAKE_GLIB "Use the bundled myglib/ GLib replacement instead of real GLib" ${WIN32})
option(LIBSPECTRUM_ENABLE_WARNINGS "Give lots of compiler warnings when using GCC/Clang" OFF)
option(LIBSPECTRUM_BUILD_TESTS "Build the test suite (test/test)" OFF)

if (WIN32)
    option(LIBSPECTRUM_USE_MSVC_STATIC_RUNTIME "Link MSVC runtime library statically" ON)
    if (NOT LIBSPECTRUM_USE_MSVC_STATIC_RUNTIME)
        set(LIBSPECTRUM_MSVC_RUNTIME_SUFFIX "DLL")
    endif ()
    if (NOT CMAKE_MSVC_RUNTIME_LIBRARY)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>${LIBSPECTRUM_MSVC_RUNTIME_SUFFIX}")
    endif ()
endif ()

include(FetchContent)
include(GNUInstallDirs)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# --- Feature detection for config.h -------------------------------------
# Mirrors the autoconf checks in configure.ac; see cmake/config.h.cmake.in.

include(CheckIncludeFile)
include(CheckSymbolExists)
include(TestBigEndian)

check_include_file(stdint.h HAVE_STDINT_H)
check_include_file(strings.h HAVE_STRINGS_H)
check_include_file(string.h HAVE_STRING_H)
check_include_file(unistd.h HAVE_UNISTD_H)

test_big_endian(WORDS_BIGENDIAN)

check_symbol_exists(snprintf "stdio.h" HAVE_SNPRINTF)
check_symbol_exists(_snprintf "stdio.h" HAVE__SNPRINTF)
check_symbol_exists(strcasecmp "string.h;strings.h" HAVE_STRCASECMP)
check_symbol_exists(_stricmp "string.h" HAVE__STRICMP)
check_symbol_exists(strncasecmp "string.h;strings.h" HAVE_STRNCASECMP)
check_symbol_exists(_strnicmp "string.h" HAVE__STRNICMP)

# Silence compiler warnings in fetched third-party code; they are not ours to fix.
function(suppress_compilation_warnings src_dir)
    get_property(_targets DIRECTORY "${src_dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(NOT _type STREQUAL "INTERFACE_LIBRARY" AND NOT _type STREQUAL "UTILITY")
            if(MSVC)
                target_compile_options(${_t} PRIVATE /w)
            else()
                target_compile_options(${_t} PRIVATE -w)
            endif()
        endif()
    endforeach()
endfunction()

if(ZLIB_SUPPORT)
    find_package(ZLIB 1.3.2 QUIET)
    if(NOT ZLIB_FOUND)
        FetchContent_Declare(
                zlib
                URL https://github.com/madler/zlib/releases/download/v1.3.2/zlib132.zip
                URL_HASH SHA256=e8bf55f3017aa181690990cb58a994e77885da140609fc8f94abe9b65d2cae28
                DOWNLOAD_NO_PROGRESS TRUE
                SYSTEM
                EXCLUDE_FROM_ALL)

        set(CACHE{ZLIB_BUILD_EXAMPLES} TYPE BOOL FORCE VALUE OFF)
        set(CACHE{ZLIB_BUILD_TESTING} TYPE BOOL FORCE VALUE OFF)

        FetchContent_MakeAvailable(zlib)
        suppress_compilation_warnings("${zlib_SOURCE_DIR}")
    endif()
    set(HAVE_ZLIB_H TRUE)
endif()

if(BZIP2_SUPPORT)
    find_package(bzip2 CONFIG QUIET)
    if(NOT bzip2_FOUND)
        FetchContent_Declare(
                bzip2
                GIT_REPOSITORY https://github.com/apwojcik/bzip2.git
                GIT_TAG master
                GIT_PROGRESS FALSE
                GIT_SHALLOW TRUE
                SYSTEM
                EXCLUDE_FROM_ALL)

        set(CACHE{ENABLE_DOCS} TYPE BOOL FORCE VALUE OFF)
        set(CACHE{ENABLE_EXAMPLES} TYPE BOOL FORCE VALUE OFF)
        set(CACHE{ENABLE_STATIC_LIB} TYPE BOOL FORCE VALUE ON)
        set(CACHE{ENABLE_STATIC_LIB_IS_PIC} TYPE BOOL FORCE VALUE ON)
        set(CACHE{ENABLE_TESTS} TYPE BOOL FORCE VALUE OFF)
        set(CACHE{ENABLE_APP} TYPE BOOL FORCE VALUE OFF)
        set(CACHE{ENABLE_SHARED_LIB} TYPE BOOL FORCE VALUE OFF)

        FetchContent_MakeAvailable(bzip2)
        suppress_compilation_warnings("${bzip2_SOURCE_DIR}")
    endif()
    set(HAVE_LIBBZ2 TRUE)
endif()

# libgcrypt: optional; crypto.c is always compiled and self-guards on
# HAVE_GCRYPT_H. CMake ships no official Find module, and configure.ac
# itself only ever checks for the header before blindly linking -lgcrypt,
# so mirror that rather than inventing a stricter check here.
if(LIBGCRYPT_SUPPORT)
    find_path(GCRYPT_INCLUDE_DIR gcrypt.h)
    find_library(GCRYPT_LIBRARY NAMES gcrypt)
    if(GCRYPT_INCLUDE_DIR AND GCRYPT_LIBRARY)
        set(HAVE_GCRYPT_H TRUE)
        list(APPEND LIBSPECTRUM_OPTIONAL_LIBS ${GCRYPT_LIBRARY})
    else()
        message(STATUS "libgcrypt not found, building without RZX signing support")
    endif()
endif()

# GLib: either real GLib (via pkg-config) or the bundled myglib/
# replacement. Windows/Cygwin/Wii autotools builds already default to the
# replacement (see CLAUDE.md); USE_FAKE_GLIB mirrors that default here so
# Windows builds don't need pkg-config + glib-2.0 dev headers installed.
set(LIBSPECTRUM_MYGLIB_SOURCES "")
if(USE_FAKE_GLIB)
    include(CheckCSourceCompiles)
    string(REPLACE ";" " " LIBSPECTRUM_C23_FLAGS_STR "${LIBSPECTRUM_C23_FLAGS}")
    set(CMAKE_REQUIRED_FLAGS "${LIBSPECTRUM_C23_FLAGS_STR}")
    check_c_source_compiles("
        #include <stdatomic.h>
        static atomic_flag lock = ATOMIC_FLAG_INIT;
        int main(void) {
          if (!atomic_flag_test_and_set_explicit(&lock, memory_order_acquire))
            atomic_flag_clear_explicit(&lock, memory_order_release);
          return 0;
        }
    " LIBSPECTRUM_HAVE_ATOMIC_FLAG)
    unset(CMAKE_REQUIRED_FLAGS)
    if(NOT LIBSPECTRUM_HAVE_ATOMIC_FLAG)
        message(FATAL_ERROR "C11 atomic flag support is required by the internal GLib replacement (myglib/); set USE_FAKE_GLIB=OFF to use real GLib instead")
    endif()
    set(LIBSPECTRUM_MYGLIB_SOURCES
        myglib/garray.c
        myglib/ghash.c
        myglib/gslist.c
        myglib/glock.c)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
    set(HAVE_LIB_GLIB TRUE)
    list(APPEND LIBSPECTRUM_OPTIONAL_LIBS PkgConfig::GLIB)
endif()

# libaudiofile: optional non-Apple WAV backend, found via pkg-config as in
# configure.ac. Not every platform building this (e.g. Windows, with
# USE_FAKE_GLIB default ON) has pkg-config available, so probe for it
# rather than requiring it outright.
set(audiofile_FOUND FALSE)
if(WAV_SUPPORT AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(AUDIOFILE QUIET IMPORTED_TARGET audiofile)
        if(AUDIOFILE_FOUND)
            set(audiofile_FOUND TRUE)
            list(APPEND LIBSPECTRUM_OPTIONAL_LIBS PkgConfig::AUDIOFILE)
        else()
            message(STATUS "libaudiofile not found, building without WAV support")
        endif()
    else()
        message(STATUS "pkg-config not found, building without WAV support")
    endif()
endif()

# Mirrors what the WAV_SUPPORT/audiofile_FOUND logic further down actually
# resolves to, so the public header's LIBSPECTRUM_SUPPORTS_WAV macro
# doesn't claim support that isn't backed by a compiled backend.
if(WAV_SUPPORT AND (APPLE OR audiofile_FOUND))
    set(HAVE_WAV_BACKEND TRUE)
endif()
if(HAVE_WAV_BACKEND AND APPLE)
    set(HAVE_MACOS_NATIVE_WAV TRUE)
    set(WAV_INTERNAL_MACOS TRUE)
elseif(HAVE_WAV_BACKEND)
    set(HAVE_LIB_AUDIOFILE TRUE)
    set(WAV_INTERNAL_AUDIOFILE TRUE)
endif()

if(WIN32)
    set(WIN32_LEAN_AND_MEAN TRUE)
endif()

set(LIBSPECTRUM_RC_VERSION "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
set(LIBSPECTRUM_COPYRIGHT "(c) 1999-2026 Philip Kendall and others")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.cmake.in
               ${CMAKE_CURRENT_BINARY_DIR}/config.h)

# libspectrum.h is generated from libspectrum.h.in; see generate.py and
# CLAUDE.md's "Generated code" section.
set(LIBSPECTRUM_H ${CMAKE_CURRENT_BINARY_DIR}/libspectrum.h)
add_custom_command(
    OUTPUT ${LIBSPECTRUM_H}
    COMMAND ${CMAKE_COMMAND}
            -DPYTHON3_EXECUTABLE=${Python3_EXECUTABLE}
            -DSRCDIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTFILE=${LIBSPECTRUM_H}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateHeader.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/generate.py
        ${CMAKE_CURRENT_SOURCE_DIR}/libspectrum.h.in
        ${CMAKE_CURRENT_SOURCE_DIR}/snap_accessors.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/tape_accessors.txt
        ${CMAKE_CURRENT_BINARY_DIR}/config.h
    COMMENT "Generating libspectrum.h"
    VERBATIM)

# snap_accessors.c / tape_accessors.c / tape_set.c are generated from the
# *_accessors.txt spec files by the matching Python script (see CLAUDE.md's
# "Generated code" section); each field declared there becomes a getter/
# setter pair that the rest of the library links against.
function(generate_accessor_source OUTVAR SCRIPT TXTFILE)
    set(outfile ${CMAKE_CURRENT_BINARY_DIR}/${OUTVAR})
    add_custom_command(
        OUTPUT ${outfile}
        COMMAND ${CMAKE_COMMAND}
                -DPYTHON3_EXECUTABLE=${Python3_EXECUTABLE}
                -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/${SCRIPT}
                -DTXTFILE=${CMAKE_CURRENT_SOURCE_DIR}/${TXTFILE}
                -DOUTFILE=${outfile}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateAccessors.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/${SCRIPT}
            ${CMAKE_CURRENT_SOURCE_DIR}/${TXTFILE}
        COMMENT "Generating ${OUTVAR}"
        VERBATIM)
endfunction()

generate_accessor_source(snap_accessors.c accessor.py snap_accessors.txt)
generate_accessor_source(tape_accessors.c tape_accessors.py tape_accessors.txt)
generate_accessor_source(tape_set.c tape_set.py tape_accessors.txt)

add_library(spectrum
    ${LIBSPECTRUM_H}
    ${CMAKE_CURRENT_BINARY_DIR}/snap_accessors.c
    ${CMAKE_CURRENT_BINARY_DIR}/tape_accessors.c
    ${CMAKE_CURRENT_BINARY_DIR}/tape_set.c
    ${LIBSPECTRUM_MYGLIB_SOURCES}
    buffer.c
    bzip2.c
    creator.c
    crypto.c
    csw.c
    csw_write.c
    dck.c
    dsnap.c
    file.c
    ide.c
    libspectrum.c
    memory.c
    microdrive.c
    mmc.c
    plusd.c
    pzx.c
    pzx_internals.h
    pzx_read.c
    pzx_write.c
    rzx.c
    sna.c
    snapshot.c
    snp.c
    sp.c
    symbol_table.c
    szx.c
    tap.c
    tap_pzx.c
    tape.c
    tape_block.c
    tape_block.h
    timings.c
    tzx_pzx.c
    tzx_read.c
    tzx_write.c
    utilities.c
    warajevo_read.c
    z80.c
    z80em.c
    zxs.c)

if(HAVE_ZLIB_H)
    target_sources(spectrum PRIVATE zip.c zip.h zlib.c)
endif()

if(APPLE)
    # Universal binary not supported, only Apple silicon
    set(CACHE{CMAKE_OSX_ARCHITECTURES}
            TYPE STRING FORCE VALUE "arm64")
endif()

target_include_directories(spectrum PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# myglib/*.c quote-include "internals.h" etc. from the project root; files
# living at the root resolve that via the including file's own directory,
# but myglib/ needs the root on the search path explicitly.
target_include_directories(spectrum PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(LIBSPECTRUM_C23_FLAGS)
    target_compile_options(spectrum PRIVATE ${LIBSPECTRUM_C23_FLAGS})
endif()

if(HAVE_GCRYPT_H)
    target_include_directories(spectrum PRIVATE ${GCRYPT_INCLUDE_DIR})
endif()

if(HAVE_ZLIB_H)
    target_link_libraries(spectrum PRIVATE ZLIB::ZLIBSTATIC)
endif()
if(HAVE_LIBBZ2)
    target_link_libraries(spectrum PRIVATE bzip2::bz2_static)
endif()
target_link_libraries(spectrum PRIVATE ${LIBSPECTRUM_OPTIONAL_LIBS})

# libspectrum_la_LIBADD has -lm.
if(NOT WIN32)
    target_link_libraries(spectrum PRIVATE m)
endif()

# AM_CFLAGS = -fvisibility=hidden; LIBSPECTRUM_EXPORTS marks the public API.
set_target_properties(spectrum PROPERTIES C_VISIBILITY_PRESET hidden)

# configure.ac warning flags for GCC (and Clang, which accepts the same ones).
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(spectrum PRIVATE
        -Wall -Wextra -Wsign-compare -Wwrite-strings)
    if(LIBSPECTRUM_ENABLE_WARNINGS)
        target_compile_options(spectrum PRIVATE
            -Wstrict-prototypes -Wmissing-prototypes -Winline -Wshadow
            -Wpointer-arith -Wcast-qual -Wcast-align -Waggregate-return)
    endif()
endif()

if(HAVE_WAV_BACKEND)
    target_sources(spectrum PRIVATE wav.c wav_internals.h)
    if (APPLE)
        target_sources(spectrum PRIVATE wav_macos.c)
    endif ()
    if(audiofile_FOUND)
        target_sources(spectrum PRIVATE wav_audiofile.c)
    endif()
endif()

if(WIN32)
    if(BUILD_SHARED_LIBS)
        target_sources(spectrum PRIVATE dll.c windres.rc)
    endif()
endif()

set_target_properties(spectrum PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

target_compile_definitions(spectrum PRIVATE LIBSPECTRUM_EXPORTS)

# --- Install ------------------------------------------------------------
# Mirrors the autotools install (lib_LTLIBRARIES, nodist_include_HEADERS,
# pkgconfig_DATA in Makefile.am): the library itself, the generated public
# header, and a pkg-config file derived from the same libspectrum.pc.in
# template autotools uses.

install(TARGETS spectrum
    EXPORT libspectrumTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES ${LIBSPECTRUM_H} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# CMake package configuration, so consumers can do
# find_package(libspectrum) and link against libspectrum::spectrum
# instead of going through pkg-config.
add_library(libspectrum::spectrum ALIAS spectrum)

include(CMakePackageConfigHelpers)

set(LIBSPECTRUM_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/libspectrum)

install(EXPORT libspectrumTargets
    FILE libspectrumTargets.cmake
    NAMESPACE libspectrum::
    DESTINATION ${LIBSPECTRUM_CMAKE_INSTALL_DIR})

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/libspectrumConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/libspectrumConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/libspectrumConfig.cmake
    INSTALL_DESTINATION ${LIBSPECTRUM_CMAKE_INSTALL_DIR})

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/libspectrumConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/libspectrumConfigVersion.cmake
    DESTINATION ${LIBSPECTRUM_CMAKE_INSTALL_DIR})

set(WAV_BACKEND_PRIVATE_LIBS "")
if(HAVE_WAV_BACKEND)
    if(APPLE)
        set(WAV_BACKEND_PRIVATE_LIBS "-framework AudioToolbox -framework CoreFoundation")
    elseif(audiofile_FOUND)
        set(WAV_BACKEND_PRIVATE_LIBS "${AUDIOFILE_LDFLAGS}")
    endif()
endif()

set(prefix ${CMAKE_INSTALL_PREFIX})
set(exec_prefix "\${prefix}")
set(libdir "\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}")
set(includedir "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
set(PACKAGE_VERSION ${PROJECT_VERSION})

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/libspectrum.pc.in
               ${CMAKE_CURRENT_BINARY_DIR}/libspectrum.pc @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libspectrum.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# --- Tests --------------------------------------------------------------
# Mirrors test/Makefile.am: test/test links a static copy of the library so
# it can reach private helpers (hidden in the shared library), and runs from
# the build directory because DYNAMIC_TEST_PATH() is relative to it.

if(LIBSPECTRUM_BUILD_TESTS)
    enable_testing()

    # Same sources/flags as the spectrum target, always static and with
    # default visibility.
    set(_test_props SOURCES INCLUDE_DIRECTORIES COMPILE_DEFINITIONS
                    COMPILE_OPTIONS)
    add_library(spectrum_test STATIC EXCLUDE_FROM_ALL)
    foreach(_p IN LISTS _test_props)
        get_target_property(_v spectrum ${_p})
        if(NOT _v STREQUAL "_v-NOTFOUND")
            set_property(TARGET spectrum_test PROPERTY ${_p} ${_v})
        endif()
    endforeach()
    # target_link_libraries (not the property) so a static library's private
    # dependencies are propagated to whatever links it.
    get_target_property(_libs spectrum LINK_LIBRARIES)
    if(NOT _libs STREQUAL "_libs-NOTFOUND")
        target_link_libraries(spectrum_test PRIVATE ${_libs})
    endif()
    # The Windows resource/DLL entry sources only make sense in the DLL.
    get_target_property(_srcs spectrum_test SOURCES)
    list(FILTER _srcs EXCLUDE REGEX "(^|/)(dll\\.c|windres\\.rc)$")
    set_property(TARGET spectrum_test PROPERTY SOURCES ${_srcs})
    set_target_properties(spectrum_test PROPERTIES C_VISIBILITY_PRESET default)

    set(LIBSPECTRUM_TEST_FIXTURES
        ${CMAKE_CURRENT_BINARY_DIR}/test/complete-tzx.tzx
        ${CMAKE_CURRENT_BINARY_DIR}/test/wav-mono-threshold.wav
        ${CMAKE_CURRENT_BINARY_DIR}/test/wav-stereo-mixdown.wav)

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test/complete-tzx.tzx
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/complete-tzx.py
                > ${CMAKE_CURRENT_BINARY_DIR}/test/complete-tzx.tzx
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/complete-tzx.py
        VERBATIM)
    foreach(_kind mono-threshold stereo-mixdown)
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test/wav-${_kind}.wav
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/wav-fixtures.py
                    ${_kind} > ${CMAKE_CURRENT_BINARY_DIR}/test/wav-${_kind}.wav
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/wav-fixtures.py
            VERBATIM)
    endforeach()

    add_executable(libspectrum_test EXCLUDE_FROM_ALL
        test/buffer.c
        test/capabilities.c
        test/common.c
        test/creator.c
        test/edges.c
        test/file.c
        test/main.c
        test/microdrive.c
        test/registry.c
        test/rzx.c
        test/snap-memory.c
        test/snap-peripherals.c
        test/snap-read.c
        test/snap-storage.c
        test/snap-z80.c
        test/szx.c
        test/tape-edges.c
        test/tape-iterator.c
        test/tape.c
        test/pzx-write.c
        test/csw-write.c
        test/tap-pzx.c
        test/timings.c
        test/utilities.c
        test/identify.c
        ${LIBSPECTRUM_TEST_FIXTURES})
    set_target_properties(libspectrum_test PROPERTIES
        OUTPUT_NAME test
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test)
    target_compile_definitions(libspectrum_test PRIVATE
        SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}" LIBSPECTRUM_EXPORTS)
    target_include_directories(libspectrum_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
    if(LIBSPECTRUM_C23_FLAGS)
        target_compile_options(libspectrum_test PRIVATE ${LIBSPECTRUM_C23_FLAGS})
    endif()
    target_link_libraries(libspectrum_test PRIVATE spectrum_test)
    if(HAVE_LIB_GLIB)
        target_link_libraries(libspectrum_test PRIVATE PkgConfig::GLIB)
    endif()
    if(HAVE_GCRYPT_H)
        target_include_directories(libspectrum_test PRIVATE ${GCRYPT_INCLUDE_DIR})
    endif()

    # `make check` equivalent: builds then runs the suite.
    add_test(NAME build_libspectrum_test
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                     --target libspectrum_test --config $<CONFIG>)
    set_tests_properties(build_libspectrum_test PROPERTIES FIXTURES_SETUP libspectrum_test_built)
    add_test(NAME libspectrum
             COMMAND libspectrum_test
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(libspectrum PROPERTIES FIXTURES_REQUIRED libspectrum_test_built)
endif()
