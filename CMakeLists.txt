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
option(WAV_SUPPORT "Enable WAV audio files" ON)
option(ZLIB_SUPPORT "Use zlib for compressed snapshot support" ON)
option(BZIP2_SUPPORT "Use libbz2 for bzip2-compressed file support" ON)
option(LIBGCRYPT_SUPPORT "Use libgcrypt for RZX signing/verification" ON)
option(USE_FAKE_GLIB "Use the bundled myglib/ GLib replacement instead of real GLib" ${WIN32})

include(FetchContent)

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

set(LIBSPECTRUM_OPTIONAL_LIBS "")

# zlib: optional, enables zip.c/zlib.c (compressed szx snapshots etc. -
# these two files are zlib-only and #include <zlib.h> unconditionally, so
# they're only added to the library below when zlib is actually found).
if(ZLIB_SUPPORT)
    find_package(ZLIB)
    if(ZLIB_FOUND)
        set(HAVE_ZLIB_H TRUE)
        list(APPEND LIBSPECTRUM_OPTIONAL_LIBS ZLIB::ZLIB)
    else()
        message(STATUS "zlib not found, building without zlib support")
    endif()
endif()

# bzip2: optional; bzip2.c is always compiled and self-guards internally
# on HAVE_LIBBZ2, so no source-list change is needed here.
if(BZIP2_SUPPORT)
    find_package(BZip2)
    if(BZIP2_FOUND)
        set(HAVE_LIBBZ2 TRUE)
        list(APPEND LIBSPECTRUM_OPTIONAL_LIBS BZip2::BZip2)
    else()
        message(STATUS "libbz2 not found, building without bzip2 support")
    endif()
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

# Mirrors what the WAV_SUPPORT/audiofile_FOUND logic further down actually
# resolves to, so the public header's LIBSPECTRUM_SUPPORTS_WAV macro
# doesn't claim support that isn't backed by a compiled backend.
if(WAV_SUPPORT AND (APPLE OR audiofile_FOUND))
    set(HAVE_WAV_BACKEND TRUE)
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

add_library(spectrum
    ${LIBSPECTRUM_H}
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

if(APPLE)
    # Universal binary not supported, only Apple silicon
    set(CACHE{CMAKE_OSX_ARCHITECTURES}
            TYPE STRING FORCE VALUE "arm64")
endif()

target_include_directories(spectrum PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
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

target_link_libraries(spectrum PRIVATE ${LIBSPECTRUM_OPTIONAL_LIBS})

if(HAVE_ZLIB_H)
    target_sources(spectrum PRIVATE zip.c zlib.c)
endif()

if(WAV_SUPPORT)
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

include(GenerateExportHeader)
generate_export_header(spectrum
        EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/export.h)

set_target_properties(spectrum PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    # libspectrum.h.in's LIBSPECTRUM_API dllexport/dllimport switch checks
    # LIBSPECTRUM_EXPORTS specifically, not CMake's default per-target
    # <name>_EXPORTS macro.
    DEFINE_SYMBOL LIBSPECTRUM_EXPORTS)