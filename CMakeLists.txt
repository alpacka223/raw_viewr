cmake_minimum_required(VERSION 3.25)

project(rawr C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

#add_compile_options(-Wall)

if(WIN32)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/windows")
else()
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/linux")
endif()

# ============================================================
# Windows / MinGW
# ============================================================

if(MINGW)

  # Statically link the GCC runtimes into our executable.
  add_link_options(
        -static
        -static-libgcc
        -static-libstdc++
    )

endif()

# ============================================================
# SDL3
# ============================================================

if(MINGW)

  message(STATUS "Building SDL3 STATIC")

  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)

  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

  add_subdirectory(
        "${CMAKE_SOURCE_DIR}/third_party/SDL3"
        "${CMAKE_BINARY_DIR}/sdl3-build"
    )

  if(NOT TARGET SDL3::SDL3-static)
    message(FATAL_ERROR
            "SDL3::SDL3-static was not created"
        )
  endif()

  if(NOT TARGET SDL3::SDL3)
    add_library(
            SDL3::SDL3
            ALIAS
            SDL3::SDL3-static
        )
  endif()

  if(NOT TARGET SDL3::Headers)
    message(FATAL_ERROR
            "SDL3::Headers was not created"
        )
  endif()

  set(SDL3_TARGET SDL3::SDL3-static)

else()

  find_package(SDL3 REQUIRED)

  set(SDL3_TARGET SDL3::SDL3)

endif()

# ============================================================
# SDL3_image
# ============================================================

if(MINGW)

  message(STATUS "Building SDL3_image STATIC")

  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)

  set(SDLIMAGE_SHARED OFF CACHE BOOL "" FORCE)
  set(SDLIMAGE_STATIC ON CACHE BOOL "" FORCE)

  add_subdirectory(
        "${CMAKE_SOURCE_DIR}/third_party/SDL3_image"
        "${CMAKE_BINARY_DIR}/sdl3-image-build"
    )

  if(NOT TARGET SDL3_image::SDL3_image-static)
    message(FATAL_ERROR
            "SDL3_image::SDL3_image-static was not created"
        )
  endif()

  set(
        SDL3_IMAGE_TARGET
        SDL3_image::SDL3_image-static
    )

else()
  find_package(SDL3_image REQUIRED)

  set(SDL3_IMAGE_TARGET SDL3_image::SDL3_image)

endif()

# ============================================================
# SDL3_ttf
# ============================================================

if(MINGW)

  message(STATUS "Building SDL3_ttf STATIC")

  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
  set(SDLTTF_SHARED OFF CACHE BOOL "" FORCE)
  set(SDLTTF_STATIC ON CACHE BOOL "" FORCE)

  add_subdirectory(
        "${CMAKE_SOURCE_DIR}/third_party/SDL3_ttf"
        "${CMAKE_BINARY_DIR}/sdl3-ttf-build"
    )

  if(TARGET SDL3_ttf::SDL3_ttf-static)

    set(
            SDL3_TTF_TARGET
            SDL3_ttf::SDL3_ttf-static
        )

  elseif(TARGET SDL3_ttf-static)

    set(
            SDL3_TTF_TARGET
            SDL3_ttf-static
        )

  else()

    message(FATAL_ERROR
            "SDL3_ttf static target was not created"
        )

  endif()

else()

  find_package(SDL3_ttf REQUIRED)

  set(SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)

endif()

# ============================================================
# LibRaw
# ============================================================

if(MINGW)

  message(STATUS "Building LibRaw STATIC")

  add_compile_definitions(
        M_PI=3.14159265358979323846
        M_SQRT1_2=0.70710678118654752440
    )

  set(
        BUILD_SHARED_LIBS
        OFF
        CACHE BOOL "" FORCE
    )

  set(
        ENABLE_OPENMP
        OFF
        CACHE BOOL "" FORCE
    )

  set(
        LIBRAW_PATH
        "${CMAKE_SOURCE_DIR}/third_party/LibRaw"
        CACHE PATH "" FORCE
    )

  add_subdirectory(
        "${CMAKE_SOURCE_DIR}/third_party/LibRaw-cmake"
        "${CMAKE_BINARY_DIR}/libraw-build"
    )

  set(LIBRAW_TARGET libraw::libraw)

else()

  find_package(PkgConfig REQUIRED)

  pkg_check_modules(
        LIBRAW
        REQUIRED
        IMPORTED_TARGET
        libraw
    )

  set(LIBRAW_TARGET PkgConfig::LIBRAW)

endif()


# ============================================================
# Your source
# ============================================================

file(
    GLOB SRCS
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.c"
)

add_executable(main ${SRCS})


# ============================================================
# Libraries
# ============================================================

target_link_libraries(
    main PRIVATE

    ${SDL3_TARGET}
    ${SDL3_IMAGE_TARGET}
    ${SDL3_TTF_TARGET}
    ${LIBRAW_TARGET}
)


# ============================================================
# Extra MinGW libraries
# ============================================================

if(MINGW)

  target_link_options(
        main PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )

  target_link_libraries(main PRIVATE
        stdc++
        gcc
    )



endif()
