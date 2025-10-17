cmake_minimum_required(VERSION 3.27)

# Project setup
project(automarker-c VERSION 0.2.2 LANGUAGES C)
set(CMAKE_C_STANDARD 99)

# debug flags
if(NOT MSVC)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -Wextra -g")
    # if not on macos, add -pg for profiling
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -pg")
    endif()
endif()

# release flags
if(MSVC)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /fp:fast /arch:AVX2")
else()
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
endif()

if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0")
  set(CMAKE_OSX_ARCHITECTURES "arm64")

  find_program(HOMEBREW_EXECUTABLE brew)
  if(NOT HOMEBREW_EXECUTABLE)
    message(WARNING "Homebrew (brew) not found. Dependencies may fail to link.")
  else()
    # Helper function to get a brew package prefix
    function(get_brew_prefix package var)
      execute_process(
        COMMAND ${HOMEBREW_EXECUTABLE} --prefix ${package}
        OUTPUT_VARIABLE BREW_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
      )
      set(${var} ${BREW_PATH} PARENT_SCOPE)
    endfunction()

    get_brew_prefix(openblas OPENBLAS_PREFIX)
    get_brew_prefix(libomp   LIBOMP_PREFIX)
    get_brew_prefix(libpng   LIBPNG_PREFIX)

    # Add brew paths for find_* commands and compilation
    list(APPEND CMAKE_PREFIX_PATH "${OPENBLAS_PREFIX}" "${LIBOMP_PREFIX}" "${LIBPNG_PREFIX}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -I${OPENBLAS_PREFIX}/include")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${OPENBLAS_PREFIX}/lib")

    # Configure OpenMP from brew
    set(OpenMP_C_FLAGS "-Xpreprocessor -fopenmp -I${LIBOMP_PREFIX}/include")
    set(OpenMP_C_LIB_NAMES "omp")
    set(OpenMP_omp_LIBRARY "${LIBOMP_PREFIX}/lib/libomp.dylib")
  endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

if(CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES ".*/vcpkg.cmake")
    find_package(SDL3 CONFIG REQUIRED)
    find_package(SDL3_image CONFIG REQUIRED)
    find_package(SDL3_ttf CONFIG REQUIRED)
    find_package(CURL CONFIG REQUIRED)
    find_package(cJSON CONFIG REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 REQUIRED sdl3)
    pkg_check_modules(SDL3_TTF REQUIRED sdl3-ttf)
    pkg_check_modules(SDL3_IMAGE REQUIRED sdl3-image)
    pkg_check_modules(CURL REQUIRED libcurl)
    pkg_check_modules(CJSON REQUIRED libcjson)
endif()

add_subdirectory(libs/SDL_sound EXCLUDE_FROM_ALL)
add_subdirectory(libs/CARA EXCLUDE_FROM_ALL)

# Add OpenBLAS include directory for CARA headers
if(EXISTS "/usr/include/openblas")
    include_directories("/usr/include/openblas")
endif()

if(APPLE)
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE
        src/main.c
        src/updater.c
        src/audio_state.c
        src/connections/process_utils.c
        src/connections/premiere_pro.c
        src/connections/after_effects.c
        src/connections/resolve.c
        src/connections/curl_manager.c
        libs/tinyfiledialogs/tinyfiledialogs.c)
else()
    add_executable(${PROJECT_NAME}
        src/main.c
        src/updater.c
        src/audio_state.c
        src/connections/process_utils.c
        src/connections/premiere_pro.c
        src/connections/after_effects.c
        src/connections/resolve.c
        src/connections/curl_manager.c
        libs/tinyfiledialogs/tinyfiledialogs.c)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION="${PROJECT_VERSION}")

if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_IDENTIFIER "com.acrilique.automarker"
        MACOSX_BUNDLE_INFO_PLIST "<string>LSApplicationCategoryType</string><string>public.app-category.utilities</string>"
    )

    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks;@loader_path/../Frameworks")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

    install(TARGETS ${PROJECT_NAME} BUNDLE DESTINATION .)
    install(TARGETS SDL3_sound LIBRARY DESTINATION lib_temp)

    set(CPACK_GENERATOR "DragNDrop")
    include(CPack)
else()
    # Set rpath for the executable to find the shared libraries at runtime
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN"
    )
endif()

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
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${SDL3_INCLUDE_DIRS}
        ${SDL3_TTF_INCLUDE_DIRS}
        ${SDL3_IMAGE_INCLUDE_DIRS}
        ${CURL_INCLUDE_DIRS}
        ${CJSON_INCLUDE_DIRS}
    )

    target_link_directories(${PROJECT_NAME} PRIVATE
        ${SDL3_LIBRARY_DIRS}
        ${SDL3_TTF_LIBRARY_DIRS}
        ${SDL3_IMAGE_LIBRARY_DIRS}
        ${CURL_LIBRARY_DIRS}
        ${CJSON_LIBRARY_DIRS}
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${SDL3_LIBRARIES}
        ${SDL3_TTF_LIBRARIES}
        ${SDL3_IMAGE_LIBRARIES}
        ${CURL_LIBRARIES}
        ${CJSON_LIBRARIES}
        SDL3_sound::SDL3_sound
        CARA::CARA
    )

    target_compile_options(${PROJECT_NAME} PRIVATE
        ${SDL3_CFLAGS_OTHER}
        ${SDL3_TTF_CFLAGS_OTHER}
        ${SDL3_IMAGE_CFLAGS_OTHER}
        ${CURL_CFLAGS_OTHER}
        ${CJSON_CFLAGS_OTHER}
    )
endif()

if(APPLE)
    # Add resources to the bundle using native CMake approach
    file(GLOB_RECURSE BUNDLE_RESOURCES "resources/*")
    target_sources(${PROJECT_NAME} PRIVATE ${BUNDLE_RESOURCES})
    set_source_files_properties(${BUNDLE_RESOURCES} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
else()
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/resources
            ${CMAKE_BINARY_DIR}/$<CONFIGURATION>/resources
    )
endif()
