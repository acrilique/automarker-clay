cmake_minimum_required(VERSION 3.27)

# Project setup
project(automarker-c C)
set(CMAKE_C_STANDARD 99)

option(MACOS_BUNDLE "Create a macOS bundle structure" OFF)

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -Wextra -g")
# if not on macos, add -pg for profiling
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -pg")
endif()
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

if(MACOS_BUNDLE)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>/lib")
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
endif()

if(CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES ".*/vcpkg.cmake")
    find_package(SDL3 CONFIG REQUIRED)
    find_package(SDL3_image CONFIG REQUIRED)
    find_package(SDL3_ttf CONFIG REQUIRED)
    find_package(CURL CONFIG REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 REQUIRED sdl3)
    pkg_check_modules(SDL3_TTF REQUIRED sdl3-ttf)
    pkg_check_modules(SDL3_IMAGE REQUIRED sdl3-image)
    pkg_check_modules(CURL REQUIRED libcurl)
endif()

add_subdirectory(libs/SDL_sound EXCLUDE_FROM_ALL)
add_subdirectory(libs/CARA EXCLUDE_FROM_ALL)

# Add OpenBLAS include directory for CARA headers
if(EXISTS "/usr/include/openblas")
    include_directories("/usr/include/openblas")
endif()

if(MACOS_BUNDLE)
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE
        src/main.c
        src/audio_state.c
        src/connections/process_utils.c
        src/connections/premiere_pro.c
        src/connections/after_effects.c
        src/connections/resolve.c
        libs/tinyfiledialogs/tinyfiledialogs.c)
else()
    add_executable(${PROJECT_NAME}
        src/main.c
        src/audio_state.c
        src/connections/process_utils.c
        src/connections/premiere_pro.c
        src/connections/after_effects.c
        src/connections/resolve.c
        libs/tinyfiledialogs/tinyfiledialogs.c)
endif()

if(MACOS_BUNDLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MACOS_BUNDLE)

    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        INSTALL_RPATH "@executable_path/../Frameworks;@loader_path/../Frameworks"
    )

    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks;@loader_path/../Frameworks")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

    install(TARGETS ${PROJECT_NAME} BUNDLE DESTINATION .)

    install(CODE "
      include(BundleUtilities)
      set(bundle \"$<INSTALL_PREFIX>/${PROJECT_NAME}.app\")
      set(search_dirs
        \"/opt/homebrew/lib\"
        \"/usr/local/lib\"
      )
      fixup_bundle(
        \"\${bundle}\" \"\" \"\${search_dirs}\"
        IGNORE_ITEM
          \"/System/Library/.*\"
          \"/usr/lib/.*\"
          \".*libSystem.*\"
          \".*libobjc.*\"
          \".*AppKit.framework/.*\"
          \".*Foundation.framework/.*\"
      )
      execute_process(COMMAND codesign --force --deep --sign - \"\${bundle}\")
    ")

    include(CPack)
    set(CPACK_GENERATOR DragNDrop)
else()
    # Set rpath for the executable to find the shared libraries at runtime
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(RPATH_VALUE "@executable_path")
    else()
        set(RPATH_VALUE "$ORIGIN")
    endif()

    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "${RPATH_VALUE}"
    )
endif()

if(CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES ".*/vcpkg.cmake")
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
        CURL::libcurl
        SDL3_sound::SDL3_sound
        CARA::CARA
    )
else()
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${SDL3_INCLUDE_DIRS}
        ${SDL3_TTF_INCLUDE_DIRS}
        ${SDL3_IMAGE_INCLUDE_DIRS}
        ${CURL_INCLUDE_DIRS}
    )

    target_link_directories(${PROJECT_NAME} PRIVATE
        ${SDL3_LIBRARY_DIRS}
        ${SDL3_TTF_LIBRARY_DIRS}
        ${SDL3_IMAGE_LIBRARY_DIRS}
        ${CURL_LIBRARY_DIRS}
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
endif()

add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/resources
		${CMAKE_BINARY_DIR}/$<CONFIGURATION>/resources
)
