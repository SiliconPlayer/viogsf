cmake_minimum_required(VERSION 3.10)

project(viogsf CXX)

include(GNUInstallDirs)

add_library(viogsf SHARED
    vbam/apu/Blip_Buffer.cpp
    vbam/apu/Effects_Buffer.cpp
    vbam/apu/Gb_Apu.cpp
    vbam/apu/Gb_Oscs.cpp
    vbam/apu/Multi_Buffer.cpp
    vbam/gba/GBA-arm.cpp
    vbam/gba/GBA-thumb.cpp
    vbam/gba/GBA.cpp
    vbam/gba/bios.cpp
    vbam/gba/Sound.cpp
)

target_include_directories(viogsf PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vbam>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viogsf/vbam>
)
target_include_directories(viogsf PRIVATE
    vbam/gba
    vbam/apu
    vbam/common
)

set_target_properties(viogsf PROPERTIES POSITION_INDEPENDENT_CODE ON)

install(TARGETS viogsf
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY vbam/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viogsf/vbam
    FILES_MATCHING PATTERN "*.h"
)
