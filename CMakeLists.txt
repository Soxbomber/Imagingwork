cmake_minimum_required(VERSION 3.14)

# ── C++ 표준 (project() 호출 전 설정) ───────────────────────────────────────
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(Imagingwork)

# ── Visual Studio 2022 / MSVC 설정 ──────────────────────────────────────────
set(CMAKE_GENERATOR_PLATFORM "x64"  CACHE STRING "" FORCE)
set(CMAKE_GENERATOR_TOOLSET  "v143" CACHE STRING "" FORCE)

# ── Qt 자동화 (모든 서브모듈에 전파) ────────────────────────────────────────
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

# ── Qt5 경로 ─────────────────────────────────────────────────────────────────
# cmake -DQt5_DIR="C:/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5" ..
# 또는 아래 주석 해제:
# set(Qt5_DIR "C:/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5")
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)

# ── IDS peak SDK ─────────────────────────────────────────────────────────────
# cmake -DCMAKE_PREFIX_PATH="C:/Program Files/IDS/ids_peak/generic_sdk/api/lib/x86_64" ..
# 또는 아래 주석 해제 후 버전 번호 수정:
# set(ids_peak_DIR
#   "C:/Program Files/IDS/ids_peak/generic_sdk/api/lib/x86_64/ids_peak-2.20.0/cmake")
# set(ids_peak_ipl_DIR
#   "C:/Program Files/IDS/ids_peak/generic_sdk/ipl/lib/x86_64/ids_peak_ipl-2.20.0/cmake")
# set(ids_peak_icv_DIR
#   "C:/Program Files/IDS/ids_peak/generic_sdk/icv/lib/x86_64/ids_peak_icv-2.20.0/cmake")
find_package(ids_peak     REQUIRED)
find_package(ids_peak_ipl REQUIRED)
find_package(ids_peak_icv REQUIRED)

# ── 서브모듈 등록 순서: 의존성 낮은 순 ─────────────────────────────────────
#   device  : deviceinfo / devicemanager / deviceitemwidget
#   viewer  : ImageViewerDock
#   camera  : acquisitionWorker / ids_camera  (viewer 의존)
#   app     : 실행 파일  (camera / device / viewer 모두 의존)
add_subdirectory(device)
add_subdirectory(viewer)
add_subdirectory(camera)
add_subdirectory(app)
