add_library(uvc STATIC
    UvcCameraDriver.h
    UvcCameraDriver.cpp
    UvcConvertAVX2.h
    UvcConvertAVX2.cpp
)
target_include_directories(uvc
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(uvc PUBLIC Qt5::Core Qt5::Multimedia device viewer)
if(MSVC)
    target_compile_options(uvc PRIVATE /utf-8)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/UvcConvertAVX2.cpp
        PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
else()
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/UvcConvertAVX2.cpp
        PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
endif()

# 출력 경로 명시 (LNK1104 방지)
set_target_properties(uvc PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
