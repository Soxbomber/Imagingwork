# app module - main window, UI, entry point

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

# Qt 리소스 (icons + CTI 포함)
set(APP_RESOURCES
    ${CMAKE_SOURCE_DIR}/resources.qrc
)

add_executable(${PROJECT_NAME} WIN32
    ${APP_SOURCES}
    ${APP_RESOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/camera          # ICameraDriver.h
    ${CMAKE_SOURCE_DIR}/camera/uvc      # UvcCameraDriver.h
    ${CMAKE_SOURCE_DIR}/camera/gige     # GigECameraDriver.h
    ${CMAKE_SOURCE_DIR}/camera/genicam  # ArvCameraDriver.h
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    camera
    uvc
    gige
    device
    viewer
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Multimedia
    Qt5::Network
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
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

# ── INSTALL 설정 ─────────────────────────────────────────────────────────────
# CMAKE_INSTALL_PREFIX 기본값(C:\Program Files)은 관리자 권한 필요 → 실패
# 빌드 폴더 내 install/ 디렉토리를 기본값으로 지정
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX
        "${CMAKE_BINARY_DIR}/install"
        CACHE PATH "Install prefix" FORCE)
endif()

# 실행 파일 설치
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# windeployqt: POST_BUILD(빌드 폴더)와 INSTALL(설치 폴더) 양쪽 실행
find_program(WINDEPLOYQT windeployqt
    HINTS "${Qt5_DIR}/../../../bin"
)
if(WINDEPLOYQT)
    # 1. POST_BUILD: 빌드 폴더에 Qt DLL 배포 (디버그 실행용)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${WINDEPLOYQT}"
            --no-compiler-runtime
            --no-translations
            "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMENT "windeployqt (build folder)..."
    )

    # 2. INSTALL 타겟: 설치 폴더에도 windeployqt 실행
    install(CODE "
        message(STATUS \"Running windeployqt on install prefix...\")
        execute_process(
            COMMAND \"${WINDEPLOYQT}\"
                --no-compiler-runtime
                --no-translations
                \"\${CMAKE_INSTALL_PREFIX}/bin/${PROJECT_NAME}.exe\"
            RESULT_VARIABLE _wdq_result
        )
        if(NOT _wdq_result EQUAL 0)
            message(WARNING \"windeployqt exited with \${_wdq_result}\")
        endif()
    ")
endif()

# vcpkg DLL 복사 (libusb, zlib 등)
# vcpkg installed 경로 자동 탐색
set(_VCPKG_BIN_DIRS
    "$ENV{VCPKG_ROOT}/installed/x64-windows/bin"
    "C:/vcpkg/installed/x64-windows/bin"
)
foreach(_vcpkg_bin IN LISTS _VCPKG_BIN_DIRS)
    if(EXISTS "${_vcpkg_bin}")
        # Release DLL만 복사 (Debug는 *d.dll 패턴)
        install(DIRECTORY "${_vcpkg_bin}/"
            DESTINATION bin
            FILES_MATCHING
                PATTERN "libusb*.dll"
                PATTERN "zlib*.dll"
                PATTERN "libzlib*.dll"
        )
        message(STATUS "[install] vcpkg DLLs from: ${_vcpkg_bin}")
        break()
    endif()
endforeach()

