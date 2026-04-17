# GenICam SDK 설정 방법

## 1. SDK ZIP 압축 해제

`GenICam_V3_5_0-Win64_x64_VC141-Release-SDK.zip`을
프로젝트 루트의 `thirdparty/genicam/` 폴더에 압축 해제합니다.

```
ImagingworkNew/
└── thirdparty/
    └── genicam/
        └── library/
            └── CPP/
                ├── include/
                │   ├── GenApi/GenApi.h        ← 헤더
                │   ├── GenApi/NodeMapFactory.h
                │   └── Base/GCBase.h
                └── lib/
                    └── Win64_x64/
                        ├── GenApi_MD_VC141_v3_5.lib   ← import lib
                        └── GCBase_MD_VC141_v3_5.lib
```

PowerShell:
```powershell
mkdir thirdparty\genicam
Expand-Archive GenICam_V3_5_0-Win64_x64_VC141-Release-SDK.zip thirdparty\genicam
```

## 2. Runtime DLL 복사

`GenICam_V3_5_0-Win64_x64_VC141-Release-Runtime.zip`의
`bin/Win64_x64/*.dll`을 빌드 출력 폴더에 복사합니다.

```powershell
# Debug 빌드의 경우
Expand-Archive GenICam_V3_5_0-Win64_x64_VC141-Release-Runtime.zip tmp_runtime
Copy-Item tmp_runtime\bin\Win64_x64\*.dll build\debug\app\
```

## 3. CMake 재구성 및 빌드

```
cmake --preset msvc2022-x64-debug
# VS에서 .sln 열고 빌드
```

## 확인

빌드 로그에 다음이 표시되면 정상:
```
-- GenICam SDK found: .../thirdparty/genicam
-- GenICam SDK linked (HAVE_GENICAM_SDK defined)
```

앱 실행 로그:
```
GigECameraDriver: GenApi SDK OK (336 nodes)
GigECameraDriver: AcquisitionStart OK
```
