cmake_minimum_required(VERSION 3.20)

project(CleanPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ---- Qt ----
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Multimedia MultimediaWidgets)

# ---- App target ----
add_executable(CleanPlayer
  main.cpp

  player/IPlayerBackend.h
  player/PlayerWidget.h
  player/PlayerWidget.cpp

  # Always available (Qt-only) backend
  player/QtMediaBackend.h
  player/QtMediaBackend.cpp
)

target_link_libraries(CleanPlayer PRIVATE
  Qt6::Widgets
  Qt6::Gui
  Qt6::Multimedia
  Qt6::MultimediaWidgets
)

# ---- Threads (Linux nicety) ----
find_package(Threads REQUIRED)
target_link_libraries(CleanPlayer PRIVATE Threads::Threads)

# ---- Optional VLC backend (auto-detect) ----
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
  pkg_check_modules(VLC QUIET IMPORTED_TARGET libvlc)
endif()

if (TARGET PkgConfig::VLC)
  message(STATUS "libVLC found: building VLC backend")
  target_sources(CleanPlayer PRIVATE
    player/VlcBackend.h
    player/VlcBackend.cpp
  )
  target_link_libraries(CleanPlayer PRIVATE PkgConfig::VLC)
  target_compile_definitions(CleanPlayer PRIVATE HAVE_VLC=1)
else()
  message(STATUS "libVLC NOT found: VLC backend will NOT be built (CLI --vlc should be handled gracefully)")
  target_compile_definitions(CleanPlayer PRIVATE HAVE_VLC=0)
endif()

# ---- Windows macro hygiene (kept from your original) ----
if (WIN32)
  target_compile_definitions(CleanPlayer PRIVATE
    NOMINMAX
    _USE_MATH_DEFINES
  )
endif()