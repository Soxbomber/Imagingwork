# ── app 모듈 ─────────────────────────────────────────────────────────────────
# 담당: 메인 윈도우, UI 구성, 진입점(main)
# 의존: camera / device / viewer 모듈 전체

set(APP_SOURCES
    main.cpp
    Imagingwork.h
    Imagingwork.cpp
    ui_Imagingwork.h
    ui_Imagingwork.cpp
    Camera_submenu.h
    Camera_submenu.cpp
    Iconlabel.h
    Iconlabel.cpp
)

# Qt 리소스는 루트에 위치
set(APP_RESOURCES
    ${CMAKE_SOURCE_DIR}/resources.qrc
)

add_executable(${PROJECT_NAME} WIN32
    ${APP_SOURCES}
    ${APP_RESOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    camera
    device
    viewer
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    ids_peak::ids_peak
    ids_peak_icv::ids_peak_icv
)

# ── IDS peak DLL 배포 ────────────────────────────────────────────────────────
# 실행 파일(${PROJECT_NAME}) 타겟에 지정해야 .exe와 같은 폴더에 DLL이 복사됨.
# camera 정적 라이브러리에 지정하면 라이브러리 출력 경로에 복사되어
# 디버깅 시 "ids_peak.dll을 찾을 수 없음" 에러가 발생함.
ids_peak_deploy(${PROJECT_NAME})
ids_peak_icv_deploy(${PROJECT_NAME})

# ── MSVC / VS 2022 설정 ──────────────────────────────────────────────────────
if(MSVC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        UNICODE
        _UNICODE
        _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    )
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3
        /MP
        /permissive-
        /Zc:__cplusplus
    )
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ── 빌드 후: Qt DLL 자동 배포 ───────────────────────────────────────────────
find_program(WINDEPLOYQT windeployqt
    HINTS "${Qt5_DIR}/../../../bin"
)
if(WINDEPLOYQT)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${WINDEPLOYQT}"
            --no-compiler-runtime
            --no-translations
            "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMENT "Running windeployqt..."
    )
endif()

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
