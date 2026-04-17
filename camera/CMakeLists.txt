add_library(camera STATIC
    ICameraDriver.h
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
    genicam/DebayerAVX2.h
    genicam/DebayerAVX2.cpp
)

target_include_directories(camera
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/genicam
)
target_link_libraries(camera PUBLIC Qt5::Core device viewer)

# ── GenICam SDK (v3.5) ────────────────────────────────────────────────────────
# SDK ZIP을 프로젝트 루트의 thirdparty/genicam 에 압축 해제:
#   thirdparty/genicam/library/CPP/include/
#   thirdparty/genicam/library/CPP/lib/Win64_x64/
set(GENICAM_SDK_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/genicam" CACHE PATH
    "GenICam SDK root (library/CPP/include + lib/Win64_x64)")

if(EXISTS "${GENICAM_SDK_ROOT}/library/CPP/include/GenApi/GenApi.h")
    message(STATUS "GenICam SDK found: ${GENICAM_SDK_ROOT}")
    target_include_directories(camera PUBLIC
        "${GENICAM_SDK_ROOT}/library/CPP/include")

    # GenICam SDK는 Release(/MD) 런타임만 제공
    # Debug(/MDd)와 혼합 시 _ITERATOR_DEBUG_LEVEL 불일치(LNK2038) 발생
    # → generator expression으로 Release 빌드에서만 lib 링크
    set(GENICAM_LIB_DIR "${GENICAM_SDK_ROOT}/library/CPP/lib/Win64_x64")
    target_link_libraries(camera PUBLIC
        "$<$<NOT:$<CONFIG:Debug>>:${GENICAM_LIB_DIR}/GenApi_MD_VC141_v3_5.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${GENICAM_LIB_DIR}/GCBase_MD_VC141_v3_5.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${GENICAM_LIB_DIR}/XmlParser_MD_VC141_v3_5.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${GENICAM_LIB_DIR}/Log_MD_VC141_v3_5.lib>"
    )
    # HAVE_GENICAM_SDK: Release에서만 정의 (스텁과 실구현 전환)
    target_compile_definitions(camera PUBLIC
        $<$<NOT:$<CONFIG:Debug>>:HAVE_GENICAM_SDK>
    )
    message(STATUS "GenICam SDK: Release=full, Debug=stub (avoids LNK2038)")

else()
    message(WARNING
        "GenICam SDK not found at ${GENICAM_SDK_ROOT}\n"
        "  → GenApiController will use fallback (ArvGenApiXml)\n"
        "  → To enable: unzip SDK to thirdparty/genicam/")
endif()

# libusb
find_package(libusb CONFIG QUIET)
if(TARGET libusb::libusb)
    target_link_libraries(camera PUBLIC libusb::libusb)
else()
    find_path(LIBUSB_INCLUDE_DIR libusb.h
        PATHS "$ENV{VCPKG_ROOT}/installed/x64-windows/include/libusb-1.0"
              "C:/vcpkg/installed/x64-windows/include/libusb-1.0")
    find_library(LIBUSB_LIBRARY NAMES libusb-1.0 usb-1.0
        PATHS "$ENV{VCPKG_ROOT}/installed/x64-windows/lib"
              "C:/vcpkg/installed/x64-windows/lib")
    if(LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARY)
        target_include_directories(camera PUBLIC ${LIBUSB_INCLUDE_DIR})
        target_link_libraries(camera PUBLIC ${LIBUSB_LIBRARY})
    else()
        message(FATAL_ERROR "libusb not found! Run: vcpkg install libusb:x64-windows")
    endif()
endif()

# zlib
find_package(ZLIB REQUIRED)
target_link_libraries(camera PUBLIC ZLIB::ZLIB)

if(MSVC)
    target_compile_options(camera PRIVATE /utf-8)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/genicam/DebayerAVX2.cpp
        PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
else()
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/genicam/DebayerAVX2.cpp
        PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
endif()

# 출력 경로 명시 (LNK1104 방지)
set_target_properties(camera PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
