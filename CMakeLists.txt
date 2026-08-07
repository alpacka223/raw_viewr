cmake_minimum_required(VERSION 3.25)
project(rawr C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall)
add_definitions(-DLIBRAW_NODLL)

if(WIN32)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/windows")
else()
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/linux")
endif()

include(FetchContent)

find_package(SDL3 QUIET)
set(SDL3_TARGET SDL3::SDL3)
if(NOT SDL3_FOUND)
  message(STATUS "SDL3 not found locally. Fetching and building...")

  FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        main
    )
  FetchContent_MakeAvailable(SDL3)
  set(SDL3_TARGET SDL3::SDL3-static)
endif()

find_package(SDL3_image QUIET)
set(SDL3_IMAGE_TARGET SDL3_image::SDL3_image)
if(NOT SDL3_image_FOUND)
  message(STATUS "SDL3_image not found locally. Fetching and building...")
  FetchContent_Declare(
        SDL3_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
        GIT_TAG        main
    )
  FetchContent_MakeAvailable(SDL3_image)
  set(SDL3_IMAGE_TARGET SDL3_image::SDL3_image-static)
endif()

find_package(SDL3_ttf QUIET)
set(SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)
if(NOT SDL3_ttf_FOUND)
  message(STATUS "SDL3_ttf not found locally. Fetching and building...")
  FetchContent_Declare(
        SDL3_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG        main
    )
  FetchContent_MakeAvailable(SDL3_ttf)
  set(SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf-static)
endif()


find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBRAW QUIET libraw_r libraw)
endif()

if(NOT LIBRAW_FOUND)
  find_path(LIBRAW_INCLUDE_DIR NAMES libraw/libraw.h HINTS /usr/local/include)
  find_library(LIBRAW_LIBRARY NAMES raw_r raw HINTS /usr/local/lib)

  if(LIBRAW_INCLUDE_DIR AND LIBRAW_LIBRARY)
    set(LIBRAW_FOUND TRUE)
    set(LIBRAW_INCLUDE_DIRS ${LIBRAW_INCLUDE_DIR})
    set(LIBRAW_LIBRARIES ${LIBRAW_LIBRARY})
  endif()
endif()

if(NOT LIBRAW_FOUND)
  message(STATUS "LibRaw not found locally or standard paths")
  FetchContent_Declare(
        LibRaw
        GIT_REPOSITORY https://github.com/LibRaw/LibRaw.git
        GIT_TAG        master
    )
  FetchContent_MakeAvailable(LibRaw)
endif()

file(GLOB SRCS "src/*.c")
add_executable(main ${SRCS})

# Link Libraries
target_link_libraries(main PRIVATE
    ${SDL3_TARGET}
    ${SDL3_IMAGE_TARGET}
    ${SDL3_TTF_TARGET}
    $<$<BOOL:${LIBRAW_FOUND}>:${LIBRAW_LIBRARIES}>
    $<$<TARGET_EXISTS:raw>:raw>
    $<$<TARGET_EXISTS:raw_r>:raw_r>
    m
)

# Static linking options for MinGW
if(MINGW)
  target_link_options(main PRIVATE -ltm -static-libgcc -Wl,-Bstatic)
endif()
