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
  # if you have libraw installed, it should be there
  list(APPEND CMAKE_PREFIX_PATH "/usr/local")
endif()

include(FetchContent)

find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
  message(STATUS "SDL3 not found. Building from source...")
  FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        main
    )
  FetchContent_MakeAvailable(SDL3)
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
  message(STATUS "LibRaw not found locally via pkg-config or headers. Fetching from source...")
  FetchContent_Declare(
      LibRaw
      GIT_REPOSITORY https://github.com/LibRaw/LibRaw.git
      GIT_TAG        master
  )
  FetchContent_MakeAvailable(LibRaw)
else()
  message(STATUS "Found installed LibRaw: ${LIBRAW_LIBRARIES}")
endif()

file(GLOB SRCS "${CMAKE_HOME_DIRECTORY}/src/*.c")
add_executable(main ${SRCS})

if(LIBRAW_INCLUDE_DIRS)
  target_include_directories(main PRIVATE ${LIBRAW_INCLUDE_DIRS})
endif()

if(TARGET SDL3::SDL3-static)
  set(SDL3_TARGET SDL3::SDL3-static)
elseif(TARGET SDL3::SDL3)
  set(SDL3_TARGET SDL3::SDL3)
else()
  set(SDL3_TARGET SDL3::SDL3_shared)
endif()

target_link_libraries(main PRIVATE
    ${SDL3_TARGET}
    $<$<BOOL:${LIBRAW_FOUND}>:${LIBRAW_LIBRARIES}>
    $<$<TARGET_EXISTS:raw>:raw>
    $<$<TARGET_EXISTS:raw_r>:raw_r>
    m
    ${CMAKE_THREAD_LIBS_INIT}
)

if(MINGW)
  target_link_options(main PRIVATE -static-libgcc -Wl,-Bstatic)
endif()
