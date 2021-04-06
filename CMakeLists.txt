cmake_minimum_required(VERSION 3.9)
# Copyright (c) Imagination Technologies Limited.

project(PVRAccelerationStructureHostCommands)

# Force Release if build type not set
if((NOT DEFINED CMAKE_BUILD_TYPE) OR (NOT CMAKE_BUILD_TYPE))
    message("-DCMAKE_BUILD_TYPE not defined. Assuming Release")
    set(CMAKE_BUILD_TYPE "Release" CACHE INTERNAL "CMAKE_BUILD_TYPE - Specifies the build type on single-configuration generators")
endif()

# Include Exports.def
# Only needed on Windows
if (WIN32)
  set(PVRAccelerationStructureHostCommands_INCLUDES Exports.def)
endif(WIN32)

# PVRAccelerationStructureHostCommands sources
set(PVRAccelerationStructureHostCommands_SRC acceleration_structure_host_commands.cpp)

# Create the library
add_library(PVRAccelerationStructureHostCommands SHARED
  ${PVRAccelerationStructureHostCommands_SRC}
  ${PVRAccelerationStructureHostCommands_INCLUDES})

# Make sure output is written to proper folders on all platforms
set_target_properties(PVRAccelerationStructureHostCommands PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Debug
  LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Debug
  RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Debug
  ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Release
  LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Release
  RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Release
  ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/RelWithDebInfo
  LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/RelWithDebInfo
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/RelWithDebInfo
  ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/MinSizeRel
  LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/MinSizeRel
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/MinSizeRel
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")

# Include vulkan headers
if((NOT DEFINED VULKAN_HEADERS_DIR) OR (NOT VULKAN_HEADERS_DIR))
  set(VULKAN_HEADERS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/External/Vulkan-Headers/include) 
endif()
target_include_directories(PVRAccelerationStructureHostCommands PUBLIC ${VULKAN_HEADERS_DIR})

# Append _debug to output library names if build target is debug
set_target_properties(PVRAccelerationStructureHostCommands
  PROPERTIES OUTPUT_NAME "PVRAccelerationStructureHostCommands$<$<CONFIG:Debug>:_debug>")

# Generate json file
function(add_layer_json name layerName jsonFile)
  # Release
  if(CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(LAYER_PATH ".\\\\${name}.dll")
  else()
    set(LAYER_PATH "./lib${name}.so")
  endif()
  set(LAYER_NAME "${layerName}")

  configure_file(${jsonFile} "${CMAKE_CURRENT_BINARY_DIR}/Release/${name}.json")
  configure_file(${jsonFile} "${CMAKE_CURRENT_BINARY_DIR}/RelWithDebInfo/${name}.json")
  configure_file(${jsonFile} "${CMAKE_CURRENT_BINARY_DIR}/MinSizeRel/${name}.json")

  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${jsonFile}")

  # Debug
  if(CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(LAYER_PATH ".\\\\${name}_debug.dll")
  else()
    set(LAYER_PATH "./lib${name}_debug.so")
  endif()
  set(LAYER_NAME "${layerName}_debug")
  configure_file(${jsonFile} "${CMAKE_CURRENT_BINARY_DIR}/Debug/${name}_debug.json")
endfunction()

set(VK_VERSION_STRING "1.0.0" CACHE STRING "Vulkan version string")
add_layer_json("PVRAccelerationStructureHostCommands"
  "VK_LAYER_POWERVR_acceleration_structure_host_commands"
  "${CMAKE_CURRENT_LIST_DIR}/acceleration_structure_host_commands.json.in")

# Specify install target
if(NOT DEFINED INSTALL_ROOT)
  set(INSTALL_ROOT "${CMAKE_CURRENT_LIST_DIR}/Install")
endif()

string(CONCAT PVRAccelerationStructureHostCommands_JSON_PATH
  "$<TARGET_FILE_DIR:PVRAccelerationStructureHostCommands>/"
  "PVRAccelerationStructureHostCommands$<$<CONFIG:Debug>:_debug>.json")

install(TARGETS PVRAccelerationStructureHostCommands DESTINATION ${INSTALL_ROOT})
install(FILES ${PVRAccelerationStructureHostCommands_JSON_PATH} DESTINATION ${INSTALL_ROOT})
