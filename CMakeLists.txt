cmake_minimum_required(VERSION 3.21)
project(uWeddingPlayer LANGUAGES CXX)

# ------------------------------------------------------------------
# 기본 설정
# ------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/utf-8 /MP /W3 /permissive-)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    # Qt 5.15.2 + 최신 MSVC(14.4x+) 의 stdext 제거 비호환을 우회.
    # 모든 번역 단위(autogen 포함) 최상단에 shim 강제 포함.
    add_compile_options("/FI${CMAKE_SOURCE_DIR}/src/compat/msvc_stdext_shim.h")
endif()

# ------------------------------------------------------------------
# Qt 5.15 찾기
#   aqtinstall 기본 경로 사용 시:
#     cmake -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64 -B build
#   또는 환경변수 Qt5_DIR 로 지정.
# ------------------------------------------------------------------
if(NOT CMAKE_PREFIX_PATH)
    if(DEFINED ENV{Qt5_DIR})
        set(CMAKE_PREFIX_PATH "$ENV{Qt5_DIR}")
    elseif(EXISTS "C:/Qt/5.15.2/msvc2019_64")
        set(CMAKE_PREFIX_PATH "C:/Qt/5.15.2/msvc2019_64")
    endif()
endif()

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets)

# obs-websocket 클라이언트용 (선택). 없으면 OBS 백엔드가 비활성화됨.
find_package(Qt5 5.15 QUIET COMPONENTS WebSockets)

# ------------------------------------------------------------------
# libVLC (선택)
#   third_party/libvlc/sdk/include/vlc/vlc.h 및
#   third_party/libvlc/sdk/lib/libvlc.lib 가 있으면 사용.
#   없으면 VideoWidget 이 stub 으로 빌드됨.
# ------------------------------------------------------------------
set(LIBVLC_ROOT "${CMAKE_SOURCE_DIR}/third_party/libvlc")
set(UWP_HAS_VLC OFF)

if(EXISTS "${LIBVLC_ROOT}/sdk/include/vlc/vlc.h"
   AND EXISTS "${LIBVLC_ROOT}/sdk/lib/libvlc.lib")
    set(UWP_HAS_VLC ON)
    message(STATUS "libVLC SDK found at ${LIBVLC_ROOT}")
else()
    message(WARNING
        "libVLC SDK not found at ${LIBVLC_ROOT}/sdk. "
        "VideoWidget will be built as a stub (no playback). "
        "Place vlc-3.0.x-win64.7z contents into third_party/libvlc/ to enable video.")
endif()

# ------------------------------------------------------------------
# OBS 백엔드 (선택, GPL 격리)
#   - Qt5::WebSockets 가 있어야 obs-websocket 클라이언트 빌드 가능.
#   - libobs 는 절대 링크하지 않음 (별도 프로세스 + 네트워크 프로토콜만).
#   - third_party/obs/ 에 포터블 OBS 런타임 배치 (gitignore; README 참고).
#     수백 MB 트리이므로 매 빌드 복사하지 않고, ObsProcessManager(O3)가
#     Settings.obs().exePath 또는 이 경로를 런타임에 해석한다.
# ------------------------------------------------------------------
set(OBS_ROOT "${CMAKE_SOURCE_DIR}/third_party/obs")
set(UWP_HAS_OBS OFF)
set(UWP_OBS_SOURCES)   # O2+ 에서 src/obs/*.{h,cpp} 가 추가됨

if(Qt5WebSockets_FOUND)
    set(UWP_HAS_OBS ON)
    set(UWP_OBS_SOURCES
        src/obs/ObsClient.h
        src/obs/ObsClient.cpp
        src/obs/ObsProcessManager.h
        src/obs/ObsProcessManager.cpp
        src/obs/ObsLiveBackend.h
        src/obs/ObsLiveBackend.cpp
    )
    message(STATUS "Qt5::WebSockets found — OBS backend buildable")
    if(EXISTS "${OBS_ROOT}/bin/64bit/obs64.exe")
        message(STATUS "Portable OBS found at ${OBS_ROOT}")
    else()
        message(STATUS
            "Portable OBS not at ${OBS_ROOT}/bin/64bit/obs64.exe — "
            "engine=obs needs it at runtime (see third_party/obs/README.md)")
    endif()
else()
    message(WARNING
        "Qt5::WebSockets not found — OBS backend disabled (engine=obs unavailable). "
        "Install the Qt WebSockets module to enable.")
endif()

# ------------------------------------------------------------------
# 소스
# ------------------------------------------------------------------
set(UWP_SOURCES
    src/main.cpp

    src/app/Application.h
    src/app/Application.cpp
    src/app/Settings.h
    src/app/Settings.cpp

    src/windows/ControlWindow.h
    src/windows/ControlWindow.cpp
    src/windows/LiveWindow.h
    src/windows/LiveWindow.cpp

    src/player/VideoWidget.h
    src/player/VideoWidget.cpp
    src/player/VlcInstance.h
    src/player/VlcInstance.cpp
    src/player/LivePlayerPool.h
    src/player/LivePlayerPool.cpp
    src/player/SnapshotCache.h
    src/player/SnapshotCache.cpp
    src/player/ImageWidget.h
    src/player/ImageWidget.cpp

    src/live/ILiveSink.h

    src/take/TransitionEffect.h
    src/take/TransitionEffect.cpp
    src/take/TakeController.h
    src/take/TakeController.cpp

    src/scene/Layer.h
    src/scene/Layer.cpp
    src/scene/SceneModel.h
    src/scene/SceneModel.cpp
    src/scene/SceneSerializer.h
    src/scene/SceneSerializer.cpp

    src/editor/PreviewCanvas.h
    src/editor/PreviewCanvas.cpp
    src/editor/LayerItem.h
    src/editor/LayerItem.cpp
    src/editor/MediaListWidget.h
    src/editor/MediaListWidget.cpp
    src/editor/PropertyPanel.h
    src/editor/PropertyPanel.cpp
)

# Phase 1 에서는 console subsystem 으로 빌드하여 qDebug 출력을 보기 쉽게 한다.
# 배포 시점에는 add_executable(... WIN32 ...) 로 전환.
add_executable(uWeddingPlayer ${UWP_SOURCES} ${UWP_OBS_SOURCES})

target_include_directories(uWeddingPlayer PRIVATE src)
target_link_libraries(uWeddingPlayer PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
)

if(UWP_HAS_VLC)
    target_compile_definitions(uWeddingPlayer PRIVATE UWP_HAS_VLC=1)
    target_include_directories(uWeddingPlayer PRIVATE "${LIBVLC_ROOT}/sdk/include")
    target_link_libraries(uWeddingPlayer PRIVATE "${LIBVLC_ROOT}/sdk/lib/libvlc.lib")

    # 실행 디렉터리로 libVLC 런타임 복사 (libvlc.dll, libvlccore.dll, plugins/)
    add_custom_command(TARGET uWeddingPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBVLC_ROOT}/libvlc.dll" "$<TARGET_FILE_DIR:uWeddingPlayer>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBVLC_ROOT}/libvlccore.dll" "$<TARGET_FILE_DIR:uWeddingPlayer>/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${LIBVLC_ROOT}/plugins" "$<TARGET_FILE_DIR:uWeddingPlayer>/plugins"
        COMMENT "Copying libVLC runtime next to uWeddingPlayer.exe"
        VERBATIM)

    # plugins.dat 캐시 생성 → libvlc_new 가 365개 플러그인을 재스캔하지 않음
    # (없으면 앱 시작 시 ~25초 블로킹). copy_directory 가 캐시를 지우므로 매번 재생성.
    if(EXISTS "${LIBVLC_ROOT}/vlc-cache-gen.exe")
        add_custom_command(TARGET uWeddingPlayer POST_BUILD
            COMMAND "${LIBVLC_ROOT}/vlc-cache-gen.exe"
                    "$<TARGET_FILE_DIR:uWeddingPlayer>/plugins"
            COMMENT "Generating libVLC plugins.dat cache"
            VERBATIM)
    else()
        message(WARNING "vlc-cache-gen.exe not found — libVLC startup will be slow")
    endif()
endif()

if(UWP_HAS_OBS)
    target_compile_definitions(uWeddingPlayer PRIVATE UWP_HAS_OBS=1)
    # 개발 빌드(build/Release 실행)에서 포터블 OBS 트리 폴백 경로.
    target_compile_definitions(uWeddingPlayer PRIVATE
        UWP_OBS_DEV_ROOT="${OBS_ROOT}")
    target_link_libraries(uWeddingPlayer PRIVATE Qt5::WebSockets)
endif()

# ------------------------------------------------------------------
# windeployqt 자동 실행 (post-build)
#   비활성화하려면: cmake -DUWP_AUTO_WINDEPLOYQT=OFF
# ------------------------------------------------------------------
option(UWP_AUTO_WINDEPLOYQT "Run windeployqt automatically after build" ON)

if(WIN32 AND UWP_AUTO_WINDEPLOYQT)
    get_target_property(_qmake_path Qt5::qmake IMPORTED_LOCATION)
    if(_qmake_path)
        get_filename_component(_qt_bin_dir "${_qmake_path}" DIRECTORY)
        find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt_bin_dir}")
        if(WINDEPLOYQT_EXE)
            add_custom_command(TARGET uWeddingPlayer POST_BUILD
                COMMAND "${WINDEPLOYQT_EXE}"
                        --verbose 0
                        --no-translations
                        --no-system-d3d-compiler
                        --no-opengl-sw
                        "$<TARGET_FILE:uWeddingPlayer>"
                COMMENT "Running windeployqt"
                VERBATIM)
        else()
            message(WARNING "windeployqt not found near ${_qt_bin_dir} — skipping auto-deploy")
        endif()
    endif()
endif()
