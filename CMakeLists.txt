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
endif()

# ------------------------------------------------------------------
# windeployqt 편의 타겟
#   cmake --build build --target deploy_qt
# ------------------------------------------------------------------
if(WIN32)
    get_target_property(_qmake_path Qt5::qmake IMPORTED_LOCATION)
    if(_qmake_path)
        get_filename_component(_qt_bin_dir "${_qmake_path}" DIRECTORY)
        find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt_bin_dir}")
        if(WINDEPLOYQT_EXE)
            add_custom_target(deploy_qt
                COMMAND "${WINDEPLOYQT_EXE}"
                        --no-translations
                        --no-system-d3d-compiler
                        --no-opengl-sw
                        "$<TARGET_FILE:uWeddingPlayer>"
                DEPENDS uWeddingPlayer
                COMMENT "Running windeployqt")
        endif()
    endif()
endif()
