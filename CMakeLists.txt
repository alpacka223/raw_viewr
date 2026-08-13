cmake_minimum_required(VERSION 3.25)

project(rawr C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall)

include(FetchContent)

if(WIN32)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/windows"
    )
else()
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/linux"
    )
endif()

find_package(SDL3 QUIET)

if(SDL3_FOUND)

  message(STATUS "Using system SDL3")

  set(SDL3_TARGET SDL3::SDL3)

else()

  message(STATUS "SDL3 not found locally. Fetching and building...")

  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG main
        GIT_SHALLOW FALSE
    )

  FetchContent_MakeAvailable(SDL3)

  set(SDL3_TARGET SDL3::SDL3)

endif()

find_package(SDL3_image QUIET)

if(SDL3_image_FOUND)

  message(STATUS "Using system SDL3_image")

  set(SDL3_IMAGE_TARGET SDL3_image::SDL3_image)

else()

  message(STATUS
        "SDL3_image not found locally. Fetching and building..."
    )

  set(SDL3IMAGE_VENDORED ON CACHE BOOL "" FORCE)

  FetchContent_Declare(
        SDL3_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
        GIT_TAG main
        GIT_SHALLOW FALSE
    )

  FetchContent_MakeAvailable(SDL3_image)

  set(SDL3_IMAGE_TARGET SDL3_image::SDL3_image)

endif()

find_package(SDL3_ttf QUIET)

if(SDL3_ttf_FOUND)

  message(STATUS "Using system SDL3_ttf")

  set(SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)

else()

  message(STATUS
        "SDL3_ttf not found locally. Fetching and building..."
    )

  set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)

  FetchContent_Declare(
        SDL3_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG main
        GIT_SHALLOW FALSE
    )

  FetchContent_MakeAvailable(SDL3_ttf)

  set(SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)

endif()

set(LIBRAW_TARGET "")

if(NOT MINGW)

  find_package(PkgConfig QUIET)

  if(PkgConfig_FOUND)

    pkg_check_modules(
            LIBRAW
            QUIET
            IMPORTED_TARGET
            libraw
        )

    if(LIBRAW_FOUND)
      message(STATUS "Using system LibRaw")
      set(LIBRAW_TARGET PkgConfig::LIBRAW)
    endif()

  endif()

endif()


if(NOT LIBRAW_TARGET)

  message(STATUS "Building LibRaw from source")

  FetchContent_Declare(
        LibRawSource
        GIT_REPOSITORY https://github.com/LibRaw/LibRaw.git
        GIT_TAG master
        GIT_SHALLOW FALSE
    )

  FetchContent_GetProperties(LibRawSource)

  if(NOT librawsource_POPULATED)

    FetchContent_Populate(LibRawSource)

  endif()

  FetchContent_Declare(
        LibRawCMake
        GIT_REPOSITORY https://github.com/LibRaw/LibRaw-cmake.git
        GIT_TAG master
        GIT_SHALLOW FALSE
    )

  FetchContent_GetProperties(LibRawCMake)

  if(NOT librawcmake_POPULATED)

    FetchContent_Populate(LibRawCMake)

  endif()

  set(
        LIBRAW_PATH
        "${librawsource_SOURCE_DIR}"
        CACHE PATH
        "Path to LibRaw source"
        FORCE
    )

  message(STATUS
        "LibRaw source: ${LIBRAW_PATH}"
    )


  add_subdirectory(
        "${librawcmake_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/libraw-build"
    )

  if(TARGET libraw::libraw)

    set(
            LIBRAW_TARGET
            libraw::libraw
        )

  elseif(TARGET raw)

    set(
            LIBRAW_TARGET
            raw
        )

  else()

    message(FATAL_ERROR
            "LibRaw was configured but no LibRaw target was created."
        )

  endif()

endif()

file(GLOB SRCS
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.c"
)

add_executable(main ${SRCS})


if(NOT TARGET ${LIBRAW_TARGET})
else()

  target_include_directories(
        main PRIVATE
        "${LIBRAW_SOURCE_DIR}"
    )

endif()

target_link_libraries(
    main PRIVATE

    ${SDL3_TARGET}
    ${SDL3_IMAGE_TARGET}
    ${SDL3_TTF_TARGET}
    ${LIBRAW_TARGET}
)

if(MINGW)

  target_link_options(
        main PRIVATE
        -static-libgcc
        -static-libstdc++
  )

endif()

if(WIN32)

  add_custom_command(
        TARGET main
        POST_BUILD

        COMMAND
            ${CMAKE_COMMAND}
            -E copy_if_different

            $<TARGET_RUNTIME_DLLS:main>

            $<TARGET_FILE_DIR:main>

        COMMAND_EXPAND_LISTS
    )

endif()
