cmake_minimum_required(VERSION 3.20)

project(CleanPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ---- Qt ----
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui)

# ---- App target ----
add_executable(CleanPlayer
  main.cpp

  player/IPlayerBackend.h
  player/PlayerWidget.h
  player/PlayerWidget.cpp

  player/VlcBackend.h
  player/VlcBackend.cpp
)

target_link_libraries(CleanPlayer PRIVATE
  Qt6::Widgets
  Qt6::Gui
)

# ---- Threads (Linux nicety) ----
find_package(Threads REQUIRED)
target_link_libraries(CleanPlayer PRIVATE Threads::Threads)

# ---- VLC backend (required) ----
find_package(PkgConfig REQUIRED)
pkg_check_modules(VLC REQUIRED IMPORTED_TARGET libvlc)

message(STATUS "libVLC found: building VLC backend")
target_link_libraries(CleanPlayer PRIVATE PkgConfig::VLC)

# ---- Windows macro hygiene (kept from your original) ----
if (WIN32)
  target_compile_definitions(CleanPlayer PRIVATE
    NOMINMAX
    _USE_MATH_DEFINES
  )
endif()