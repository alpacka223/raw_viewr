#include <SDL3/SDL_stdinc.h>
#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>

#include <stdatomic.h>

#include <libraw/libraw.h>
#include <libraw/libraw_const.h>

const Uint32 INC_OFFSET_KEY = SDLK_RIGHT;
const Uint32 DEC_OFFSET_KEY = SDLK_LEFT;

constexpr double TIME_BETWEEN_OFFSET_ADDS = 200 / 1000.; // 200 ms
constexpr double TIME_BEFORE_HELD = 50 / 1000.;          // 50 ms

typedef enum : char {
  RAWR_SUCCESS = 0,
  RAWR_IMAGE_NOT_SUPPORTED,
  RAWR_LIBRAW_FAIL,
  RAWR_SDL_FAIL,
} LoadStatus;

static struct {
  SDL_Window *window; // basically const, set at the startup before the thread
                      // initialization
  SDL_Renderer *renderer; // same

  SDL_Texture *cur_image;
  SDL_Texture *cur_thumbnail;
  float cur_aspect_ratio;

  float next_aspect_ratio;
  SDL_Texture *next_image;
  SDL_Texture *next_thumbnail;

  libraw_data_t *img;

  const char *dropped_filename;
  const char *current_dir;

  Uint64 _last_perf_count;

  double delta_time; // in seconds

  atomic_int next_photo_offset;

  int libraw_error_code;

  bool is_dropping_file;

  _Atomic bool has_dropped_file;
  _Atomic bool texture_swap_ready;
  _Atomic bool load_image_failed;
  LoadStatus load_status;
  bool has_thumbnail;
  bool show_thumbnail;
} state;

static struct {
  double change_offset_timer;

  double inc_before_held_timer;
  double dec_before_held_timer;

  bool inc_key_was_held;
  bool dec_key_was_held;
} buttons;

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  switch (event->type) {

  case SDL_EVENT_QUIT:
    return SDL_APP_SUCCESS;

  case SDL_EVENT_DROP_BEGIN:
    state.is_dropping_file = true;
    break;

  case SDL_EVENT_DROP_FILE:
    state.is_dropping_file = false;
    state.dropped_filename = event->drop.data;
    atomic_store_explicit(&state.has_dropped_file, true, memory_order_release);
    break;

  case SDL_EVENT_MOUSE_BUTTON_UP:
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    break;

  case SDL_EVENT_KEY_DOWN:
    if (event->key.key == INC_OFFSET_KEY) {
      if (buttons.inc_key_was_held && buttons.inc_before_held_timer <= 0.) {
        if (buttons.change_offset_timer <= 0.) {
          atomic_fetch_add_explicit(&state.next_photo_offset, 1,
                                    memory_order_relaxed);
          buttons.change_offset_timer = TIME_BETWEEN_OFFSET_ADDS;
        }
        break;
      }
      buttons.inc_key_was_held = true;
      buttons.inc_before_held_timer = TIME_BEFORE_HELD;
    } else if (event->key.key == DEC_OFFSET_KEY) {
      if (buttons.dec_key_was_held && buttons.dec_before_held_timer <= 0.) {
        if (buttons.change_offset_timer <= 0.) {
          atomic_fetch_add_explicit(&state.next_photo_offset, -1,
                                    memory_order_relaxed);
          buttons.change_offset_timer = TIME_BETWEEN_OFFSET_ADDS;
        }
        break;
      }
      buttons.dec_key_was_held = true;
      buttons.dec_before_held_timer = TIME_BEFORE_HELD;
    }
    break;

  case SDL_EVENT_KEY_UP:
    if (event->key.key == INC_OFFSET_KEY) {
      buttons.inc_key_was_held = false;
      buttons.inc_before_held_timer = 0.;
    } else if (event->key.key == DEC_OFFSET_KEY) {
      buttons.dec_key_was_held = false;
      buttons.dec_before_held_timer = 0.;
    }
    break;
  }

  return SDL_APP_CONTINUE;
}

// should only be called by the main thread
void swap_textures() {
  SDL_DestroyTexture(state.cur_thumbnail);
  SDL_DestroyTexture(state.cur_image);

  state.cur_thumbnail = state.next_thumbnail;
  state.cur_image = state.next_image;
  state.cur_aspect_ratio = state.next_aspect_ratio;

  SDL_SetWindowAspectRatio(state.window, state.cur_aspect_ratio,
                           state.cur_aspect_ratio);

  state.next_thumbnail = nullptr;
  state.next_image = nullptr;
  state.next_aspect_ratio = 16.f / 9.f;

  atomic_store_explicit(&state.texture_swap_ready, false, memory_order_release);
}

LoadStatus try_sdl_image(const char *file_path) {}

LoadStatus load_image(const char *file_path) {
  state.libraw_error_code = libraw_open_file(state.img, file_path);
  if (state.libraw_error_code == LIBRAW_FILE_UNSUPPORTED)
    return try_sdl_image(file_path);
  else if (state.libraw_error_code != LIBRAW_SUCCESS)
    return RAWR_LIBRAW_FAIL;

  if ((state.libraw_error_code = libraw_unpack_thumb(state.img)) !=
      LIBRAW_SUCCESS)
    return RAWR_LIBRAW_FAIL;

  if ((state.libraw_error_code = libraw_unpack(state.img)) != LIBRAW_SUCCESS)
    return RAWR_LIBRAW_FAIL;

  SDL_Texture *result_thumb;
  { // get thumbnail
    SDL_IOStream *io =
        SDL_IOFromMem(state.img->thumbnail.thumb, state.img->thumbnail.tlength);
    if (!io)
      return RAWR_SDL_FAIL;

    result_thumb = IMG_LoadTexture_IO(state.renderer, io, true);
    if (!result_thumb)
      return RAWR_SDL_FAIL;
  }

  SDL_Texture *result;
  { // get processed image
    state.img->params.use_camera_wb = true;
    state.img->params.output_color = 1;

    if ((state.libraw_error_code = libraw_dcraw_process(state.img)) !=
        LIBRAW_SUCCESS) {
      goto libraw_fail;
    }

    libraw_processed_image_t *image_buffer =
        libraw_dcraw_make_mem_image(state.img, &state.libraw_error_code);
    if (state.libraw_error_code) {
      goto libraw_fail;
    }

    SDL_IOStream *io =
        SDL_IOFromMem(image_buffer->data, image_buffer->data_size);
    if (!io) {
      libraw_dcraw_clear_mem(image_buffer);
      goto sdl_fail;
    }

    result = IMG_LoadTexture_IO(state.renderer, io, true);
    if (!result) {
      libraw_dcraw_clear_mem(image_buffer);
      goto sdl_fail;
    }
  }

  state.next_image = result;
  state.next_thumbnail = result_thumb;
  state.next_aspect_ratio = (float)result->w / result->h;

  atomic_store_explicit(&state.texture_swap_ready, true, memory_order_release);
  return RAWR_SUCCESS;

sdl_fail:
  SDL_DestroyTexture(result_thumb);
  return RAWR_SDL_FAIL;

libraw_fail:
  SDL_DestroyTexture(result_thumb);
  return RAWR_LIBRAW_FAIL;
}

void show_error(const char *error_title, const char *message) {
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, error_title, message,
                           state.window);
}

char fmt_buffer[512];
SDL_AppResult SDL_AppIterate(void *appstate) {
  Uint64 now = SDL_GetPerformanceCounter();
  state.delta_time = (double)(now - state._last_perf_count) /
                     (double)SDL_GetPerformanceFrequency();
  state._last_perf_count = now;

  if (buttons.change_offset_timer > 0.) {
    buttons.change_offset_timer -= state.delta_time;
  }

  if (buttons.inc_before_held_timer > 0.) {
    buttons.inc_before_held_timer -= state.delta_time;
  }

  if (buttons.dec_before_held_timer > 0.) {
    buttons.dec_before_held_timer -= state.delta_time;
  }

  if (atomic_load_explicit(&state.load_image_failed, memory_order_acquire)) {
    switch (state.load_status) {
    case RAWR_SUCCESS:
      sprintf(fmt_buffer,
              "Something happened.\nHere is SDL get error:%s\nHere is LIBRAW "
              "error:%s\nThis should ve not happened ever.\n",
              SDL_GetError(), libraw_strerror(state.libraw_error_code));
      show_error("WTF", fmt_buffer);
      break;

    case RAWR_IMAGE_NOT_SUPPORTED:
      show_error("Not supported", "This image type is not supported.");
      break;

    case RAWR_LIBRAW_FAIL:
      sprintf(fmt_buffer, "Libraw error %d while opening file:\n%s",
              state.libraw_error_code,
              libraw_strerror(state.libraw_error_code));
      show_error("Libraw error", fmt_buffer);
      break;

    case RAWR_SDL_FAIL:
      sprintf(fmt_buffer, "SDL error while opening file:\n%s", SDL_GetError());
      show_error("SDL error", fmt_buffer);
      break;
    }
  }

  if (atomic_load_explicit(&state.texture_swap_ready, memory_order_acquire))
    swap_textures();

  SDL_RenderClear(state.renderer);
  if (state.show_thumbnail) {
    if (state.has_thumbnail) {
      SDL_RenderTexture(state.renderer, state.cur_thumbnail, NULL, NULL);
    } else {
    }
  } else if (state.cur_image) {
    SDL_RenderTexture(state.renderer, state.cur_image, NULL, NULL);
  } else {
    int width;
    int height;
    SDL_GetWindowSize(state.window, &width, &height);
  }
  SDL_RenderPresent(state.renderer);

  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) || !TTF_Init()) {
    SDL_Log("SDL Init Failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  if (!SDL_CreateWindowAndRenderer("Rawr", 800, 600,
                                   SDL_WINDOW_RESIZABLE |
                                       SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                   &state.window, &state.renderer)) {
    SDL_Log("Window/Renderer Creation Failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  state._last_perf_count = SDL_GetPerformanceCounter();

  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {}
