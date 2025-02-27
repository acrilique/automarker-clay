cmake_minimum_required(VERSION 3.27)

# Project setup
project(automarker-c C)
set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

add_subdirectory(libs/SDL EXCLUDE_FROM_ALL)
add_subdirectory(libs/SDL_ttf EXCLUDE_FROM_ALL)
add_subdirectory(libs/SDL_image EXCLUDE_FROM_ALL)

# Aubio library setup needs to be done manually as it is not a CMake project
set(AUBIO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/aubio/src")
set(AUBIO_LIBRARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/aubio/build/src")

# Include aubio header directory
include_directories(${AUBIO_INCLUDE_DIR})

# Add aubio library directory
link_directories(${AUBIO_LIBRARY_DIR})

# Set rpath for the executable to find the shared libraries at runtime
set(CMAKE_INSTALL_RPATH "$ORIGIN")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

# Example executable
add_executable(${PROJECT_NAME} src/main.c libs/tinyfiledialogs.c)
target_link_libraries(${PROJECT_NAME} PRIVATE
	SDL3::SDL3
	SDL3_ttf::SDL3_ttf
	SDL3_image::SDL3_image
	aubio
)

add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/resources
		${CMAKE_BINARY_DIR}/$<CONFIGURATION>/resources
    COMMAND ${CMAKE_COMMAND} -E copy
        ${AUBIO_LIBRARY_DIR}/libaubio.so
        ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIGURATION>/libaubio.so
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libaubio.so
        ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIGURATION>/libaubio.so.5
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libaubio.so
        ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIGURATION>/libaubio.so.5.4.8

)
