cmake_minimum_required(VERSION 3.27)

# --- Project Definition ---
project(automarker-c VERSION 2.0.0 LANGUAGES C)
set(CMAKE_C_STANDARD 99)

# --- Output and Installation Directories ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install path" FORCE)

# --- Platform and Compiler Settings ---
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0")
endif()

if(MSVC)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /fp:fast /arch:AVX2")
else()
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -Wextra -g")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -pg")
    endif()
endif()

# --- Subprojects via FetchContent ---
include(FetchContent)

set(SDLSOUND_BUILD_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL_sound
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/SDL_sound
)

FetchContent_Declare(
    CARA
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/CARA
)

set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    cjson
    GIT_REPOSITORY https://github.com/p1k0chu/cJSON.git
    GIT_TAG        fix/cmake-version-bump
)

FetchContent_MakeAvailable(SDL_sound CARA cjson)

# --- External Dependencies ---
if(CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES ".*/vcpkg.cmake")
    find_package(SDL3 CONFIG REQUIRED)
    find_package(SDL3_image CONFIG REQUIRED)
    find_package(SDL3_ttf CONFIG REQUIRED)
    find_package(CURL CONFIG REQUIRED)
    set(LINK_LIBRARIES SDL3::SDL3 SDL3_image::SDL3_image SDL3_ttf::SDL3_ttf CURL::libcurl)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 REQUIRED sdl3)
    pkg_check_modules(SDL3_image REQUIRED sdl3-image)
    pkg_check_modules(SDL3_ttf REQUIRED sdl3-ttf)
    pkg_check_modules(CURL REQUIRED libcurl)

    link_directories(${SDL3_LIBRARY_DIRS} ${SDL3_image_LIBRARY_DIRS} ${SDL3_ttf_LIBRARY_DIRS} ${CURL_LIBRARY_DIRS})
    set(LINK_LIBRARIES ${SDL3_LIBRARIES} ${SDL3_image_LIBRARIES} ${SDL3_ttf_LIBRARIES} ${CURL_LIBRARIES})
endif()

if(APPLE)
  find_library(ACCELERATE_FRAMEWORK Accelerate REQUIRED)
  find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
  find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
  find_library(COREHAPTICS_FRAMEWORK CoreHaptics)
  find_library(METAL_FRAMEWORK Metal)
  list(APPEND LINK_LIBRARIES
      ${ACCELERATE_FRAMEWORK} ${AUDIOTOOLBOX_FRAMEWORK} ${COREAUDIO_FRAMEWORK}
      ${COREHAPTICS_FRAMEWORK} ${METAL_FRAMEWORK}
  )
endif()


# --- Executable Definition ---
set(SOURCES
    src/main.c
    src/updater.c
    src/audio_state.c
    src/connections/process_utils.c
    src/connections/premiere_pro.c
    src/connections/after_effects.c
    src/connections/resolve.c
    src/connections/curl_manager.c
    libs/tinyfiledialogs/tinyfiledialogs.c
)

if(APPLE)
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES})
else()
    add_executable(${PROJECT_NAME} ${SOURCES})
endif()

# --- Target Properties ---
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION="${PROJECT_VERSION}")

target_include_directories(${PROJECT_NAME} PRIVATE
    ${cjson_SOURCE_DIR}
    ${SDL3_INCLUDE_DIRS}
    ${SDL3_image_INCLUDE_DIRS}
    ${SDL3_ttf_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${LINK_LIBRARIES}
    cjson
    SDL3_sound::SDL3_sound
    CARA::CARA
)

# --- Installation and Bundling ---
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "@executable_path/../Frameworks"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.acrilique.automarker"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.acrilique.automarker"
        MACOSX_BUNDLE_BUNDLE_NAME "automarker-c"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/automarker-c.entitlements"
        XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
        XCODE_ATTRIBUTE_SKIP_INSTALL "No"
    )

    file(GLOB_RECURSE BUNDLE_RESOURCES "resources/*")
    set_source_files_properties(${BUNDLE_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(${PROJECT_NAME} PRIVATE ${BUNDLE_RESOURCES})

    install(TARGETS ${PROJECT_NAME} BUNDLE DESTINATION .)
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN"
    )
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/resources
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources
    )
endif()
