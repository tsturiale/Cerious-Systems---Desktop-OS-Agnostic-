cmake_minimum_required(VERSION 3.24)

project(cerious_qt_terminal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)

add_executable(cerious_qt_terminal
  src/GatewayClient.cpp
  src/GatewayClient.hpp
  src/MainWindow.cpp
  src/MainWindow.hpp
  src/main.cpp
)

target_link_libraries(cerious_qt_terminal PRIVATE
  Qt6::Widgets
  Qt6::Network
)

target_compile_definitions(cerious_qt_terminal PRIVATE
  QT_NO_CAST_FROM_ASCII
  QT_NO_CAST_TO_ASCII
)

if(WIN32)
  target_compile_definitions(cerious_qt_terminal PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(cerious_qt_terminal PRIVATE /W4 /permissive-)
else()
  target_compile_options(cerious_qt_terminal PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS cerious_qt_terminal
  RUNTIME DESTINATION bin
  BUNDLE DESTINATION .
)
