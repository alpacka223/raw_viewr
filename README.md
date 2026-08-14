# RAW viewer

Cross-platform SDL3 desktop application to view all kinds of photos.

## Building

### For Unix

Install with your favourite package manager:

- SDL3
- SDL_image
- SDL_ttf
- LibRaw

Then run:

```bash
cmake -S . -B build
cmake --build build 
```

### MinGW cross compilation

Install dependencies with:

```bash
git submodule update --init --recursive ./third_party/SDL3_image/ ./third_party/SDL3_ttf/
```

Set CMAKE_FIND_ROOT_PATH to your MinGW home and preffered compilers in [mingw-toolchain.cmake](./mingw-toolchain.cmake).

Then run:

```bash
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=mingw-toolchain.cmake
cmake --build build-win -j$(nproc)
```

## Usage

You can drag and drop files and folders to open them.  

You can also use os' Open with... functionality.

Keybinds:  

- Left arrow/Right arrow - choose previous/next file.  
- P - switch to a preview of raw file.  
- CTRL + S - save current view as output.jpg  
