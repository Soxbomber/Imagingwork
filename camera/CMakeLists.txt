# ── camera 모듈 ──────────────────────────────────────────────────────────────
# 담당: AcquisitionWorker (워커 스레드), IdsCamera (카메라 제어)
# 의존: IDS peak SDK, Qt5::Core, device 모듈, viewer 모듈

add_library(camera STATIC
    acquisitionWorker.h
    acquisitionWorker.cpp
    ids_camera.h
    ids_camera.cpp
)

target_include_directories(camera
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(camera
    PUBLIC
        Qt5::Core
        ids_peak::ids_peak
        ids_peak_ipl::ids_peak_ipl
        ids_peak_icv::ids_peak_icv
        device
        viewer
)

# ※ ids_peak_deploy / ids_peak_icv_deploy 는 정적 라이브러리가 아닌
#   실행 파일 타겟에 지정해야 .exe 폴더에 DLL이 복사됨 → app/CMakeLists.txt 참고
