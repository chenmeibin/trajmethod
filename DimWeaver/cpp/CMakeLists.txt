cmake_minimum_required(VERSION 3.16)
project(DimWeaverCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(dimweaver
    src/dimweaver.cpp
)

target_include_directories(dimweaver PUBLIC include)

add_executable(dimweaver_cli
    src/main.cpp
)

target_link_libraries(dimweaver_cli PRIVATE dimweaver)
