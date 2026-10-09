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

option(BUILD_SHARED_LIBS "Build libspectrum shared library" ON)
option(WAV_SUPPORT "Enable WAV audio files" ON)

include(FetchContent)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

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
    COMMENT "Generating libspectrum.h"
    VERBATIM)

add_library(spectrum
    ${LIBSPECTRUM_H}
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
    SOVERSION ${PROJECT_VERSION_MAJOR})