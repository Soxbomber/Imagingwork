# ── device 모듈 ──────────────────────────────────────────────────────────────
# 담당: deviceinfo / devicemanager / deviceitemwidget
# 의존: Qt5::Widgets (QWidget, QLabel 등)

add_library(device STATIC
    deviceinfo.h
    devicemanager.h
    devicemanager.cpp
    deviceitemwidget.h
    deviceitemwidget.cpp
)

target_include_directories(device
    PUBLIC
        # 이 모듈의 헤더를 다른 모듈이 #include "devicemanager.h" 형태로 사용 가능
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(device
    PUBLIC
        Qt5::Core
        Qt5::Widgets
)
