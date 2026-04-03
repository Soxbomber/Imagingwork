# ── viewer 모듈 ──────────────────────────────────────────────────────────────
# 담당: ImageViewerDock (QDockWidget 기반 이미지 표시 위젯)
# 의존: Qt5::Widgets

add_library(viewer STATIC
    ImageViewerDock.h
    ImageViewerDock.cpp
)

target_include_directories(viewer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(viewer
    PUBLIC
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
)
