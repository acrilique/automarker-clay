cmake_minimum_required(VERSION 3.27)

# Project setup
project(automarker-c C)
set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -Wextra -pg -g")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED sdl3)
pkg_check_modules(SDL3_TTF REQUIRED sdl3-ttf)
pkg_check_modules(SDL3_IMAGE REQUIRED sdl3-image)
add_subdirectory(libs/SDL_sound EXCLUDE_FROM_ALL)
add_subdirectory(libs/CARA EXCLUDE_FROM_ALL)
pkg_check_modules(CURL REQUIRED libcurl)

# Add OpenBLAS include directory for CARA headers
if(EXISTS "/usr/include/openblas")
    include_directories("/usr/include/openblas")
endif()

# Set rpath for the executable to find the shared libraries at runtime
set(CMAKE_INSTALL_RPATH "$ORIGIN")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

# Example executable
add_executable(${PROJECT_NAME} src/main.c src/audio_state.c src/connections/process_utils.c src/connections/premiere_pro.c src/connections/after_effects.c src/connections/resolve.c libs/tinyfiledialogs/tinyfiledialogs.c)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${SDL3_INCLUDE_DIRS}
    ${SDL3_TTF_INCLUDE_DIRS}
    ${SDL3_IMAGE_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${SDL3_LIBRARIES}
    ${SDL3_TTF_LIBRARIES}
    ${SDL3_IMAGE_LIBRARIES}
    ${CURL_LIBRARIES}
    SDL3_sound::SDL3_sound
	CARA::CARA
)

target_compile_options(${PROJECT_NAME} PRIVATE
    ${SDL3_CFLAGS_OTHER}
    ${SDL3_TTF_CFLAGS_OTHER}
    ${SDL3_IMAGE_CFLAGS_OTHER}
    ${CURL_CFLAGS_OTHER}
)

add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/resources
		${CMAKE_BINARY_DIR}/$<CONFIGURATION>/resources
)
