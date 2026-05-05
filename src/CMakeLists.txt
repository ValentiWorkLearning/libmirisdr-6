# Copyright 2012 OSMOCOM Project
#
# This file is part of MiriSDR
#
# GNU Radio is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.
#
# GNU Radio is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GNU Radio; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street,
# Boston, MA 02110-1301, USA.

# Ensure dependencies are available in this scope
if(BUILD_TEST_APPS)
    find_package(Threads REQUIRED)
endif()

########################################################################
# Setup library
########################################################################
add_library(mirisdr_shared SHARED
    libmirisdr.c
)

# Find or create imported target for libusb if needed
if(NOT TARGET libusb)
    if(LibUSB_FOUND AND LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARIES)
        add_library(libusb INTERFACE IMPORTED)
        set_target_properties(libusb PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBUSB_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${LIBUSB_LIBRARIES}"
        )
    endif()
endif()

if(BUILD_TEST_APPS)
target_link_libraries(mirisdr_shared
    PRIVATE ${LIBUSB_LIBRARIES} Threads::Threads
)

set_target_properties(mirisdr_shared PROPERTIES DEFINE_SYMBOL "mirisdr_EXPORTS")
set_target_properties(mirisdr_shared PROPERTIES OUTPUT_NAME mirisdr)
set_target_properties(mirisdr_shared PROPERTIES SOVERSION ${MAJOR_VERSION})
set_target_properties(mirisdr_shared PROPERTIES VERSION ${LIBVER})

# Set up public include directory
target_include_directories(mirisdr_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
)
endif(BUILD_TEST_APPS)

add_library(mirisdr_static STATIC
    libmirisdr.c
)

if(WIN32)
add_library(libgetopt_static STATIC
    getopt/getopt.c
)
endif()

target_link_libraries(mirisdr_static
    PRIVATE ${LIBUSB_LIBRARIES}
)

set_property(TARGET mirisdr_static APPEND PROPERTY COMPILE_DEFINITIONS "mirisdr_STATIC" )

if(NOT WIN32)
# Force same library filename for static and shared variants of the library
set_target_properties(mirisdr_static PROPERTIES OUTPUT_NAME mirisdr)
endif()

# Set up public include directory
target_include_directories(mirisdr_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
)

########################################################################
# Build utility
########################################################################
if(BUILD_TEST_APPS)

add_executable(miri_sdr miri_sdr.c)
add_executable(miri_fm miri_fm.c)
set(INSTALL_TARGETS mirisdr_shared mirisdr_static miri_sdr miri_fm)

target_link_libraries(miri_sdr PRIVATE mirisdr_shared
    ${LIBUSB_LIBRARIES}
    Threads::Threads
)

target_link_libraries(miri_fm PRIVATE mirisdr_shared
    ${LIBUSB_LIBRARIES}
    Threads::Threads
)

if(UNIX)
target_link_libraries(miri_fm PRIVATE m)
endif()

if(WIN32)
target_link_libraries(miri_sdr PRIVATE libgetopt_static)
target_link_libraries(miri_fm PRIVATE libgetopt_static)
set_property(TARGET miri_sdr APPEND PROPERTY COMPILE_DEFINITIONS "mirisdr_STATIC" )
set_property(TARGET miri_fm APPEND PROPERTY COMPILE_DEFINITIONS "mirisdr_STATIC" )
endif()

endif(BUILD_TEST_APPS)

########################################################################
# Create namespace aliases for use in both build tree and via find_package
########################################################################
if(BUILD_TEST_APPS)
    add_library(mirisdr::mirisdr_shared ALIAS mirisdr_shared)
endif(BUILD_TEST_APPS)

add_library(mirisdr::mirisdr_static ALIAS mirisdr_static)

########################################################################
# Install built library files & utilities
########################################################################
if(BUILD_TEST_APPS)
    if(LIBMIRISDR_ENABLE_INSTALL)
        install(TARGETS mirisdr_shared mirisdr_static
            EXPORT mirisdr_targets
            LIBRARY DESTINATION lib${LIB_SUFFIX} # .so/.dylib file
            ARCHIVE DESTINATION lib${LIB_SUFFIX} # .lib file
            RUNTIME DESTINATION bin              # .dll file
        )
    endif(LIBMIRISDR_ENABLE_INSTALL)
else()
    if(LIBMIRISDR_ENABLE_INSTALL)
        install(TARGETS mirisdr_static
            EXPORT mirisdr_targets
            LIBRARY DESTINATION lib${LIB_SUFFIX} # .so/.dylib file
            ARCHIVE DESTINATION lib${LIB_SUFFIX} # .lib file
            RUNTIME DESTINATION bin              # .dll file
        )
    endif(LIBMIRISDR_ENABLE_INSTALL)
endif(BUILD_TEST_APPS)
