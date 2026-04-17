add_library(gige STATIC
    GigEProtocol.h
    GigEDevice.h
    GigEDevice.cpp
    GigEStream.h
    GigEStream.cpp
    GigECameraDriver.h
    GigECameraDriver.cpp
)
target_include_directories(gige
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../genicam
)
target_link_libraries(gige PUBLIC Qt5::Core Qt5::Network camera device viewer)
if(MSVC)
    target_compile_options(gige PRIVATE /utf-8)
endif()

# 출력 경로 명시 (LNK1104 방지)
set_target_properties(gige PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
