# camera module
#
# Aravis-pattern USB3 Vision driver (libusb-1.0 + GenICam XML parsing)
#
# Prerequisites:
#   vcpkg install libusb:x64-windows zlib:x64-windows
#
# Build:
#   cmake -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
#         -DQt5_DIR="C:/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5" ..
#
# Windows driver:
#   Zadig -> select camera -> WinUSB -> Replace Driver

add_library(camera STATIC
    ICameraDriver.h

    # Aravis-pattern USB3 Vision (libusb-1.0)
    genicam/ArvU3vProtocol.h
    genicam/ArvU3vDevice.h
    genicam/ArvU3vDevice.cpp
    genicam/ArvU3vStream.h
    genicam/ArvU3vStream.cpp
    genicam/ArvGenApiXml.h
    genicam/ArvGenApiXml.cpp
    genicam/GenApiController.h
    genicam/GenApiController.cpp
    genicam/ArvCameraDriver.h
    genicam/ArvCameraDriver.cpp

    # AVX2 가속 Bayer debayer
    genicam/DebayerAVX2.h
    genicam/DebayerAVX2.cpp
)

target_include_directories(camera
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/genicam
)

target_link_libraries(camera
    PUBLIC
        Qt5::Core
        device
        viewer
)
# Note: QXmlStreamReader is part of Qt5::Core, Qt5::Xml is NOT needed

# ── libusb-1.0 ───────────────────────────────────────────────────────────────
find_package(libusb CONFIG QUIET)
if(TARGET libusb::libusb)
    message(STATUS "[camera] libusb found via CONFIG")
    target_link_libraries(camera PUBLIC libusb::libusb)
else()
    find_path(LIBUSB_INCLUDE_DIR libusb.h
        PATHS
            "$ENV{VCPKG_ROOT}/installed/x64-windows/include/libusb-1.0"
            "C:/vcpkg/installed/x64-windows/include/libusb-1.0"
    )
    find_library(LIBUSB_LIBRARY NAMES libusb-1.0 usb-1.0
        PATHS
            "$ENV{VCPKG_ROOT}/installed/x64-windows/lib"
            "C:/vcpkg/installed/x64-windows/lib"
    )
    if(LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARY)
        message(STATUS "[camera] libusb found: ${LIBUSB_LIBRARY}")
        target_include_directories(camera PUBLIC ${LIBUSB_INCLUDE_DIR})
        target_link_libraries(camera PUBLIC ${LIBUSB_LIBRARY})
    else()
        message(FATAL_ERROR
            "[camera] libusb not found!\n"
            "  Run: vcpkg install libusb:x64-windows")
    endif()
endif()

# ── zlib (XML ZIP 해제) ───────────────────────────────────────────────────────
find_package(ZLIB REQUIRED)
message(STATUS "[camera] zlib found: ${ZLIB_VERSION_STRING}")
target_link_libraries(camera PUBLIC ZLIB::ZLIB)

if(MSVC)
    target_compile_options(camera PRIVATE /utf-8)
    target_compile_definitions(camera PRIVATE UNICODE _UNICODE)
    # DebayerAVX2.cpp 전용 /arch:AVX2
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/genicam/DebayerAVX2.cpp
        PROPERTIES COMPILE_OPTIONS "/arch:AVX2"
    )
else()
    # GCC/Clang AVX2
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/genicam/DebayerAVX2.cpp
        PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma"
    )
endif()
