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
add_executable(uWeddingPlayer ${UWP_SOURCES})

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
