# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required( VERSION 3.20 )

project( libucc-dbus
  VERSION ${CMAKE_PROJECT_VERSION}
  DESCRIPTION "DBus client library for uccd daemon"
  LANGUAGES CXX
)

set( CMAKE_CXX_STANDARD 20 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

# Find dependencies
find_package( Qt6 REQUIRED COMPONENTS Core DBus )

# Library sources
add_library( ucc-dbus SHARED
  UccdClient.cpp
)
if(UCC_READ_ONLY_PREVIEW)
  target_compile_definitions(ucc-dbus PUBLIC UCC_READ_ONLY_PREVIEW)
  # A preview must never resolve the installed write-enabled client library.
  set_target_properties(ucc-dbus PROPERTIES OUTPUT_NAME ucc-dbus-preview)
endif()

target_include_directories( ucc-dbus
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries( ucc-dbus
  PUBLIC
    Qt6::Core
    Qt6::DBus
)

target_compile_options( ucc-dbus PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Werror
)

# Set library properties
set_target_properties( ucc-dbus PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Installation
include( GNUInstallDirs )
install( TARGETS ucc-dbus
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
