cmake_minimum_required(VERSION 3.10)
project(typewriter C)

set(CMAKE_C_STANDARD 11)

# Release by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Optimization flags
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

add_executable(typewriter main.c)

# Find SDL2
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
    target_include_directories(typewriter PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS})
    target_link_libraries(typewriter ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARIES} m)
else()
    # Windows / vcpkg / manual
    find_package(SDL2 REQUIRED)
    find_package(SDL2_ttf REQUIRED)
    target_link_libraries(typewriter SDL2::SDL2 SDL2::SDL2main SDL2_ttf::SDL2_ttf m)
endif()

# On Windows, use WinMain entry point
if(WIN32)
    set_target_properties(typewriter PROPERTIES WIN32_EXECUTABLE FALSE)
endif()
