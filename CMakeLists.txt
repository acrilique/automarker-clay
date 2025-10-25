cmake_minimum_required(VERSION 3.27)

# Set a local install prefix to avoid permission issues
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install path" FORCE)

project(automarker-c VERSION 0.2.3 LANGUAGES C)
set(CMAKE_C_STANDARD 99)

set(SDLSOUND_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)

if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0")
endif()

if(NOT MSVC)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -Wextra -g")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -pg")
    endif()
endif()
if(MSVC)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /fp:fast /arch:AVX2")
else()
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
endif()

# --- Output Directories ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")


# --- Find Packages ---
if(CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES ".*/vcpkg.cmake")
    find_package(SDL3 CONFIG REQUIRED)
    find_package(SDL3_image CONFIG REQUIRED)
    find_package(SDL3_ttf CONFIG REQUIRED)
    find_package(CURL CONFIG REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 REQUIRED sdl3)
    pkg_check_modules(SDL3_image REQUIRED sdl3-image)
    pkg_check_modules(SDL3_ttf REQUIRED sdl3-ttf)
    pkg_check_modules(CURL REQUIRED libcurl)

    # Add library directories for pkg-config libraries
    link_directories(${SDL3_LIBRARY_DIRS} ${SDL3_image_LIBRARY_DIRS} ${SDL3_ttf_LIBRARY_DIRS} ${CURL_LIBRARY_DIRS})
endif()

if(APPLE)
  find_library(ACCELERATE_FRAMEWORK Accelerate REQUIRED)
  find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
  find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
  find_library(COREHAPTICS_FRAMEWORK CoreHaptics)
  find_library(METAL_FRAMEWORK Metal)
endif()


# --- Subprojects via FetchContent ---
include(FetchContent)

FetchContent_Declare(
    SDL_sound
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/SDL_sound
)
FetchContent_Declare(
    CARA
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/CARA
)
FetchContent_Declare(
    cjson
    GIT_REPOSITORY https://github.com/p1k0chu/cJSON.git
    GIT_TAG        fix/cmake-version-bump
)

FetchContent_MakeAvailable(SDL_sound CARA cjson)


# --- Executable Definition ---
if(APPLE)
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE
        src/main.c src/updater.c src/audio_state.c
        src/connections/process_utils.c src/connections/premiere_pro.c
        src/connections/after_effects.c src/connections/resolve.c
        src/connections/curl_manager.c libs/tinyfiledialogs/tinyfiledialogs.c
    )
else()
    add_executable(${PROJECT_NAME}
        src/main.c src/updater.c src/audio_state.c
        src/connections/process_utils.c src/connections/premiere_pro.c
        src/connections/after_effects.c src/connections/resolve.c
        src/connections/curl_manager.c libs/tinyfiledialogs/tinyfiledialogs.c
    )
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION="${PROJECT_VERSION}")

target_include_directories(${PROJECT_NAME} PRIVATE
    ${cjson_SOURCE_DIR}
    ${SDL3_INCLUDE_DIRS}
    ${SDL3_image_INCLUDE_DIRS}
    ${SDL3_ttf_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
)

# --- Linking Libraries to Main Executable ---
# Link against the official targets provided by find_package and the subprojects.
if(CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES ".*/vcpkg.cmake")
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
        CURL::libcurl
        cjson
        SDL3_sound::SDL3_sound
        CARA::CARA
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${SDL3_LIBRARIES}
        ${SDL3_image_LIBRARIES}
        ${SDL3_ttf_LIBRARIES}
        ${CURL_LIBRARIES}
        cjson
        SDL3_sound::SDL3_sound
        CARA::CARA
    )
endif()

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${ACCELERATE_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${COREHAPTICS_FRAMEWORK}
        ${METAL_FRAMEWORK}
    )
endif()


# --- macOS Bundle and Installation ---
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        # RPATH settings for bundle
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        INSTALL_RPATH "@executable_path/../Frameworks"

        # Bundle Info
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.acrilique.automarker"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.acrilique.automarker"
        MACOSX_BUNDLE_BUNDLE_NAME "automarker-c"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        
        # Hardened Runtime
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES"
        
        # Code Signing (configure these for CI/CD with GitHub secrets)
        # XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application"
        # XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "YourTeamID"
        # XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER "YourProfileName"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/automarker-c.entitlements"
        
        # Install - these are mostly for archive
        XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
        XCODE_ATTRIBUTE_SKIP_INSTALL "No"
    )
    install(TARGETS ${PROJECT_NAME} BUNDLE DESTINATION .)

    file(GLOB_RECURSE BUNDLE_RESOURCES "resources/*")
    set_source_files_properties(${BUNDLE_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(${PROJECT_NAME} PRIVATE ${BUNDLE_RESOURCES})

    # --- macOS Library Bundling ---
    # The fixup_bundle script is now run manually after the archive is created.
    # No extra build phases are needed here.
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN")
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/resources
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources
    )
endif()
