# GigE Vision 드라이버
# Qt5::Network 사용 (QUdpSocket, QNetworkInterface)
# 추가 라이브러리 불필요 (표준 네트워크 스택)

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
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..          # ICameraDriver.h
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../genicam  # ArvGenApiXml.h, DebayerAVX2.h
)

target_link_libraries(gige
    PUBLIC
        Qt5::Core
        Qt5::Network
        camera        # ArvGenApiXml, DebayerAVX2
        device
        viewer
)

if(MSVC)
    target_compile_options(gige PRIVATE /utf-8)
endif()
