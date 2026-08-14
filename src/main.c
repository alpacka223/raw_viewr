#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>

#include <SDL3/SDL_filesystem.h>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_timer.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>

#include <stdatomic.h>

#include <libraw/libraw.h>
#include <libraw/libraw_const.h>

#define EMBEDDED_FONT 1

#if EMBEDDED_FONT
#include "font.h"
#else
const char *FONT_PATH = "";
#endif

const float FONT_SIZE = 24.f;

const Uint32 INC_OFFSET_KEY = SDLK_RIGHT;
const Uint32 DEC_OFFSET_KEY = SDLK_LEFT;

constexpr double TIME_BETWEEN_OFFSET_ADDS = 500 / 1000.; // ms

typedef enum : char {
  RAWR_SUCCESS = 0,
  RAWR_IMAGE_NOT_SUPPORTED,
  RAWR_LIBRAW_FAIL,
  RAWR_SDL_FAIL,
  RAWR_NO_IMAGE,
} RawrStatus;

typedef enum : char {
  RAWR_THREAD_QUIT,
  RAWR_THREAD_GET_PHOTO_FROM_DROP,
  RAWR_THREAD_GET_NEXT_PHOTO,
  RAWR_THREAD_ENUM_DIR_FROM_DROP,
  RAWR_THREAD_SAVE_CURRENT,
} WakeUpPurpose;

static struct {
  SDL_Window *window; // basically const, set at the startup before the thread
                      // initialization
  SDL_Renderer *renderer; // same

  SDL_Thread *thread;

  TTF_TextEngine *engine;
  TTF_Font *font;
  TTF_Text *no_thumb_text;
  int thumb_text_w, thumb_text_h;

  TTF_Text *no_image_text;
  int image_text_w, image_text_h;

  SDL_Texture *cur_image;
  SDL_Surface *cur_image_surface;

  SDL_Texture *cur_thumbnail;
  SDL_Surface *cur_thumbnail_surface;

  SDL_Surface *next_image;
  SDL_Surface *next_thumbnail;

  libraw_data_t *img;

  SDL_Mutex *mutex;
  SDL_Condition *cond;
  const char *dropped_path;
  const char *current_dir;
  char **enumerated_files;

  Uint64 _last_perf_count;

  double delta_time; // in seconds

  int enumerated_files_length;
  int cur_filename_idx;

  int next_photo_offset;

  int libraw_error_code;

  bool is_dropping_file;

  _Atomic bool texture_swap_ready;

  _Atomic bool task_failed;
  RawrStatus task_status;

  WakeUpPurpose purpose;

  bool show_thumbnail;

  char file_sep;
} state;

static struct {
  double change_offset_timer;

  bool inc_key_was_held;
  bool dec_key_was_held;
} buttons;

const char *get_dir_of(const char *file_path, const char **filename_out) {
  const char *output = ".";

  const char *last_slash = SDL_strrchr(file_path, '/');
  const char *last_backslash = SDL_strrchr(file_path, '\\');
  const char *split_point =
      (last_slash > last_backslash) ? last_slash : last_backslash;

  if (split_point != NULL) {
    size_t dir_len = split_point - file_path;

    if (dir_len == 0) {
      dir_len = 1;
    }

    output = SDL_strndup(file_path, dir_len + 1);
    if (filename_out != nullptr) {
      size_t length = SDL_strlen(file_path);
      *filename_out = SDL_strndup(file_path + dir_len + 1, length);
    }
  }
  return output;
}

bool handle_path(const char *data) {
  SDL_PathInfo p;
  SDL_GetPathInfo(data, &p);
  if (p.type == SDL_PATHTYPE_FILE) {
    SDL_free(state.enumerated_files);
    state.enumerated_files = nullptr;

    SDL_free((void *)state.current_dir);
    state.current_dir = nullptr;

    SDL_free((void *)state.dropped_path);
    state.dropped_path = SDL_strdup(data);

    state.purpose = RAWR_THREAD_GET_PHOTO_FROM_DROP;
  } else if (p.type == SDL_PATHTYPE_DIRECTORY) {
    SDL_free(state.enumerated_files);
    state.enumerated_files = nullptr;

    SDL_free((void *)state.current_dir);
    state.current_dir = SDL_strdup(data);
    state.purpose = RAWR_THREAD_ENUM_DIR_FROM_DROP;
  } else {
    atomic_store_explicit(&state.task_failed, true, memory_order_release);
    state.task_status = RAWR_IMAGE_NOT_SUPPORTED;
    return false;
  }
  return true;
}

void show_error_box(const char *error_title, const char *message) {
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, error_title, message,
                           state.window);
}

void add_photo_offset(int offset) {
  SDL_LockMutex(state.mutex);
  state.next_photo_offset += offset;
  state.purpose = RAWR_THREAD_GET_NEXT_PHOTO;
  SDL_SignalCondition(state.cond);
  SDL_UnlockMutex(state.mutex);
  buttons.change_offset_timer = TIME_BETWEEN_OFFSET_ADDS;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  switch (event->type) {

  case SDL_EVENT_QUIT:
    return SDL_APP_SUCCESS;

  case SDL_EVENT_DROP_BEGIN:
    state.is_dropping_file = true;
    break;

  case SDL_EVENT_DROP_FILE:
    state.is_dropping_file = false;
    SDL_LockMutex(state.mutex);
    if (handle_path(event->drop.data))
      SDL_SignalCondition(state.cond);
    SDL_UnlockMutex(state.mutex);
    break;

  case SDL_EVENT_MOUSE_BUTTON_UP:
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    break;

  case SDL_EVENT_KEY_DOWN:
    if (event->key.key == INC_OFFSET_KEY) {
      if (buttons.inc_key_was_held || buttons.change_offset_timer <= 0.) {
        if (state.current_dir) {
          add_photo_offset(1);
        } else {
          show_error_box("Open an image/directory first!",
                         "No image or directory opened.");
        }
        break;
      }
      buttons.inc_key_was_held = true;
    } else if (event->key.key == DEC_OFFSET_KEY) {
      if (!buttons.dec_key_was_held || buttons.change_offset_timer <= 0.) {
        if (state.current_dir) {
          add_photo_offset(-1);
        } else {
          show_error_box("Open an image/directory first!",
                         "No image or directory opened.");
        }
        break;
      }
      buttons.dec_key_was_held = true;
    }
    break;

  case SDL_EVENT_KEY_UP:
    if (event->key.key == INC_OFFSET_KEY) {
      buttons.inc_key_was_held = false;
      buttons.change_offset_timer = 0.;
    } else if (event->key.key == DEC_OFFSET_KEY) {
      buttons.dec_key_was_held = false;
      buttons.change_offset_timer = 0.;
    } else if (event->key.key == SDLK_P) {
      state.show_thumbnail = !state.show_thumbnail;
    } else if (event->key.key == SDLK_S && event->key.mod & SDL_KMOD_CTRL) {
      SDL_LockMutex(state.mutex);
      state.purpose = RAWR_THREAD_SAVE_CURRENT;
      SDL_SignalCondition(state.cond);
      SDL_UnlockMutex(state.mutex);
    }
    break;
  }

  return SDL_APP_CONTINUE;
}

// should only be called by the main thread
void swap_textures() {
  SDL_DestroyTexture(state.cur_thumbnail);
  SDL_DestroyTexture(state.cur_image);

  SDL_DestroySurface(state.cur_image_surface);
  SDL_DestroySurface(state.cur_thumbnail_surface);

  state.cur_thumbnail =
      SDL_CreateTextureFromSurface(state.renderer, state.next_thumbnail);
  state.cur_image =
      SDL_CreateTextureFromSurface(state.renderer, state.next_image);

  state.cur_image_surface = state.next_image;
  state.cur_thumbnail_surface = state.next_thumbnail;

  state.next_thumbnail = nullptr;
  state.next_image = nullptr;

  atomic_store_explicit(&state.texture_swap_ready, false, memory_order_release);
}

RawrStatus try_sdl_image(const char *file_path) {
  state.next_image = IMG_Load(file_path);
  state.next_thumbnail = nullptr;
  if (!state.next_image) {
    return RAWR_IMAGE_NOT_SUPPORTED;
  }
  atomic_store_explicit(&state.texture_swap_ready, true, memory_order_release);
  return RAWR_SUCCESS;
}

RawrStatus load_image(const char *file_path) {
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

  SDL_Surface *result_thumb;
  { // get thumbnail
    SDL_IOStream *io =
        SDL_IOFromMem(state.img->thumbnail.thumb, state.img->thumbnail.tlength);
    if (!io)
      return RAWR_SDL_FAIL;

    result_thumb = IMG_Load_IO(io, true);
    if (!result_thumb)
      return RAWR_SDL_FAIL;
  }

  SDL_Surface *result;
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

    // FIXME: make it work with different buffer types instead of just bitmap
    // switch (image_buffer->type) {
    //
    // }
    SDL_PixelFormat format = (image_buffer->colors == 4)
                                 ? SDL_PIXELFORMAT_RGBA32
                                 : SDL_PIXELFORMAT_RGB24;

    int pitch =
        image_buffer->width * image_buffer->colors * (image_buffer->bits / 8);

    result = SDL_CreateSurfaceFrom(image_buffer->width, image_buffer->height,
                                   format, image_buffer->data, pitch);
  }

  state.next_image = result;
  state.next_thumbnail = result_thumb;

  atomic_store_explicit(&state.texture_swap_ready, true, memory_order_release);
  return RAWR_SUCCESS;

libraw_fail:
  SDL_DestroySurface(result_thumb);
  return RAWR_LIBRAW_FAIL;
}

int find_idx(const char **values, int length, const char *key) {
  for (int i = 0; i < length; i++) {
    if (!SDL_strcmp(values[i], key))
      return i;
  }
  return -1;
}

const char *get_absolute_path(const char *file_dir, const char *filename,
                              char separator) {
  char *result = nullptr;
  int length_in_bytes =
      SDL_asprintf(&result, "%s%c%s", file_dir, separator, filename);
  if (!result || length_in_bytes < 0) {
    SDL_Log("Couldn't get absolute path: %s", SDL_GetError());
  } else {
    const char needle[3] = {separator, separator, '\0'};
    char *double_sep;
    while ((double_sep = SDL_strstr(result, needle))) {
      while (*double_sep) {
        *double_sep = *(double_sep + 1);
        double_sep++;
      }
    }
  }
  return result;
}

void handle_save() {
  SDL_Log("Starting save...");
  SDL_Surface *cur_surface = state.show_thumbnail && state.cur_thumbnail_surface
                                 ? state.cur_thumbnail_surface
                                 : state.cur_image_surface;
  if (!cur_surface) {
    state.task_status = RAWR_NO_IMAGE;
    atomic_store_explicit(&state.task_failed, true, memory_order_release);
    return;
  }

  const char *path =
      get_absolute_path(state.current_dir, "output.jpg", state.file_sep);

  if (!IMG_SaveJPG(cur_surface, path, 100)) {
    state.task_status = RAWR_SDL_FAIL;
    atomic_store_explicit(&state.task_failed, true, memory_order_release);
    return;
  }
  SDL_free((void *)path);
  SDL_Log("Saved!");
}

static const char *FILE_PATTERN = "*";
int SDLCALL file_handler_func(void *) {
  SDL_LockMutex(state.mutex);
  SDL_SignalCondition(state.cond); // to sync the main thread
  while (true) {
    SDL_WaitCondition(state.cond, state.mutex);
    switch (state.purpose) {
    case RAWR_THREAD_QUIT:
      goto end;

    case RAWR_THREAD_GET_PHOTO_FROM_DROP:
      SDL_Log("Getting photo from drop...");
      state.task_status = load_image(state.dropped_path);
      if (state.task_status != RAWR_SUCCESS) {
        atomic_store_explicit(&state.task_failed, true, memory_order_release);
      }

      const char *filename = nullptr;
      state.current_dir = get_dir_of(state.dropped_path, &filename);
      state.enumerated_files = SDL_GlobDirectory(
          state.current_dir, FILE_PATTERN, SDL_GLOB_CASEINSENSITIVE,
          &state.enumerated_files_length);

      state.cur_filename_idx =
          find_idx((const char **)state.enumerated_files,
                   state.enumerated_files_length, filename);

      if (state.cur_filename_idx == -1) {
        SDL_Log("Couldn't find %s in directory %s\nSetting file idx to 0.",
                filename, state.current_dir);
        state.cur_filename_idx = 0;
      }

      SDL_free((void *)state.dropped_path);
      state.dropped_path = nullptr;
      break;

    case RAWR_THREAD_GET_NEXT_PHOTO:
      SDL_Log("Getting next photo...");
      state.cur_filename_idx =
          (state.cur_filename_idx + state.next_photo_offset) %
          state.enumerated_files_length;
      if (state.cur_filename_idx < 0)
        state.cur_filename_idx += state.enumerated_files_length;

      const char *abs_path = get_absolute_path(
          state.current_dir, state.enumerated_files[state.cur_filename_idx],
          state.file_sep);

      state.task_status = load_image(abs_path);
      if (state.task_status != RAWR_SUCCESS) {
        atomic_store_explicit(&state.task_failed, true, memory_order_release);
      }
      SDL_free((void *)abs_path);
      state.next_photo_offset = 0;
      break;

    case RAWR_THREAD_ENUM_DIR_FROM_DROP:
      SDL_Log("Opening dropped folder...");
      state.enumerated_files = SDL_GlobDirectory(
          state.current_dir, FILE_PATTERN, SDL_GLOB_CASEINSENSITIVE,
          &state.enumerated_files_length);
      state.cur_filename_idx = 0;
      if (state.cur_filename_idx < state.enumerated_files_length) {
        const char *abs_path = get_absolute_path(
            state.current_dir, state.enumerated_files[state.cur_filename_idx],
            state.file_sep);
        SDL_Log("Trying to load image with absolute path of: %s", abs_path);
        state.task_status = load_image(abs_path);
        if (state.task_status != RAWR_SUCCESS) {
          atomic_store_explicit(&state.task_failed, true, memory_order_release);
        }
        SDL_free((void *)abs_path);
      }
      break;

    case RAWR_THREAD_SAVE_CURRENT:
      handle_save();
      break;
    }
  }

end:
  SDL_UnlockMutex(state.mutex);
  return 0;
}

void fill_dst_rect(float w, float h, SDL_FRect *rect_out,
                   SDL_Texture *texture) {
  float texture_aspect = (float)texture->w / texture->h;
  float window_aspect = w / h;

  if (window_aspect > texture_aspect) {
    rect_out->h = h;
    rect_out->w = h * texture_aspect;
  } else {
    rect_out->w = w;
    rect_out->h = w / texture_aspect;
  }

  rect_out->x = (w - rect_out->w) / 2.0f;
  rect_out->y = (h - rect_out->h) / 2.0f;
}

static constexpr int buffer_size = 512;
char fmt_buffer[buffer_size];

SDL_AppResult SDL_AppIterate(void *) {
  Uint64 now = SDL_GetPerformanceCounter();
  state.delta_time = (double)(now - state._last_perf_count) /
                     (double)SDL_GetPerformanceFrequency();
  state._last_perf_count = now;

  if (buttons.change_offset_timer > 0.) {
    buttons.change_offset_timer -= state.delta_time;
  }

  if (atomic_load_explicit(&state.task_failed, memory_order_acquire)) {
    switch (state.task_status) {
    case RAWR_SUCCESS:
      SDL_snprintf(
          fmt_buffer, buffer_size,
          "Something happened.\nHere is SDL get error:%s\nHere is LIBRAW "
          "error:%s\nThis should ve not happened ever.\n",
          SDL_GetError(), libraw_strerror(state.libraw_error_code));
      show_error_box("WTF", fmt_buffer);
      break;

    case RAWR_IMAGE_NOT_SUPPORTED:
      show_error_box("Not supported", "This image type is not supported.");
      break;

    case RAWR_LIBRAW_FAIL:
      SDL_snprintf(
          fmt_buffer, buffer_size, "Libraw error %d while opening file:\n%s",
          state.libraw_error_code, libraw_strerror(state.libraw_error_code));
      show_error_box("Libraw error", fmt_buffer);
      break;

    case RAWR_SDL_FAIL:
      SDL_snprintf(fmt_buffer, buffer_size, "SDL error while opening file:\n%s",
                   (const char *)SDL_GetError());
      show_error_box("SDL error", fmt_buffer);
      break;

    case RAWR_NO_IMAGE:
      show_error_box("No image open!", "Open an image or something.");
      break;
    }

    atomic_store_explicit(&state.task_failed, false, memory_order_release);
  }

  if (atomic_load_explicit(&state.texture_swap_ready, memory_order_acquire)) {
    SDL_LockMutex(state.mutex);
    swap_textures();
    SDL_UnlockMutex(state.mutex);
  }

  SDL_SetRenderDrawColor(state.renderer, 41, 41, 41, 255);
  SDL_RenderClear(state.renderer);

  int w_width, w_height;
  SDL_GetWindowSizeInPixels(state.window, &w_width, &w_height);

  if (state.show_thumbnail) {
    if (state.cur_thumbnail) {
      SDL_FRect dst_rect;
      fill_dst_rect(w_width, w_height, &dst_rect, state.cur_thumbnail);
      SDL_RenderTexture(state.renderer, state.cur_thumbnail, nullptr,
                        &dst_rect);
    } else {
      SDL_SetRenderDrawColor(state.renderer, 255, 255, 255, 255);
      TTF_DrawRendererText(state.no_thumb_text,
                           w_width / 2.f - state.thumb_text_w / 2.f,
                           w_height / 2.f - state.thumb_text_h / 2.f);
    }
  } else if (state.cur_image) {
    SDL_FRect dst_rect;
    fill_dst_rect(w_width, w_height, &dst_rect, state.cur_image);
    SDL_RenderTexture(state.renderer, state.cur_image, nullptr, &dst_rect);
  } else {
    SDL_SetRenderDrawColor(state.renderer, 255, 255, 255, 255);
    TTF_DrawRendererText(state.no_image_text,
                         w_width / 2.f - state.image_text_w / 2.f,
                         w_height / 2.f - state.image_text_h / 2.f);
  }

  if (!SDL_RenderPresent(state.renderer)) {
    SDL_Log("SDL_RenderTexture failed: %s", SDL_GetError());
  }

  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppInit(void **, int argc, char *argv[]) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) || !TTF_Init()) {
    SDL_Log("SDL Init Failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  const char *current_dir;
  if (!(current_dir = SDL_GetCurrentDirectory())) {
    SDL_Log("Couldn't get current directory: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  state.file_sep = SDL_strrchr(current_dir, '\\') == NULL ? '/' : '\\';
  SDL_free((void *)current_dir);

  state.img = libraw_init(0);
  if (!state.img) {
    SDL_Log("Couldn't initialize libraw.");
    return SDL_APP_FAILURE;
  }

#if EMBEDDED_FONT
  SDL_IOStream *io = SDL_IOFromMem(FONT_DATA, FONT_DATA_LENGTH);
  if (!io) {
    SDL_Log("Failed to get iostream of embedded font: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  state.font = TTF_OpenFontIO(io, true, FONT_SIZE);
#else
  state.font = TTF_OpenFont(FONT_PATH, FONT_SIZE);
#endif

  if (!state.font) {
    SDL_Log("Failed to open embedded font: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  if (!SDL_CreateWindowAndRenderer("Rawr", 800, 600,
                                   SDL_WINDOW_RESIZABLE |
                                       SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                   &state.window, &state.renderer)) {
    SDL_Log("Window/Renderer Creation Failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  state.engine = TTF_CreateRendererTextEngine(state.renderer);

  if (!state.engine) {
    SDL_Log("Couldn't create ttf engine: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  state.no_image_text =
      TTF_CreateText(state.engine, state.font, "No image.", 0);

  if (!state.no_image_text) {
    SDL_Log("Couldn't create no image text: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  TTF_GetTextSize(state.no_image_text, &state.image_text_w,
                  &state.image_text_h);

  state.no_thumb_text =
      TTF_CreateText(state.engine, state.font, "No thumbnail.", 0);

  if (!state.no_thumb_text) {
    SDL_Log("Couldn't create no thumb text: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  TTF_GetTextSize(state.no_thumb_text, &state.thumb_text_w,
                  &state.thumb_text_h);

  state._last_perf_count = SDL_GetPerformanceCounter();

  state.cond = SDL_CreateCondition();
  state.mutex = SDL_CreateMutex();

  state.thread = SDL_CreateThread(file_handler_func, "file-handler", NULL);
  if (!state.thread) {
    SDL_Log("Couldn't initialize a thread.");
    return SDL_APP_FAILURE;
  }

  if (argc > 1) {
    SDL_LockMutex(state.mutex);
    SDL_WaitCondition(state.cond, state.mutex);
    if (handle_path(argv[1])) {
      SDL_SignalCondition(state.cond);
    }
    SDL_UnlockMutex(state.mutex);
  }

  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *, SDL_AppResult result) {
  SDL_LockMutex(state.mutex);
  state.purpose = RAWR_THREAD_QUIT;
  SDL_SignalCondition(state.cond);
  SDL_UnlockMutex(state.mutex);

  int status;
  SDL_WaitThread(state.thread, &status);
  SDL_Log("Thread exited with: %d", status);

  SDL_DestroyTexture(state.cur_image);
  SDL_DestroyTexture(state.cur_thumbnail);

  SDL_DestroySurface(state.next_image);
  SDL_DestroySurface(state.next_thumbnail);

  SDL_DestroyCondition(state.cond);
  SDL_DestroyMutex(state.mutex);

  SDL_free((void *)state.dropped_path);
  SDL_free((void *)state.current_dir);

  TTF_DestroyText(state.no_image_text);
  TTF_DestroyText(state.no_thumb_text);

  TTF_CloseFont(state.font);
  TTF_DestroyRendererTextEngine(state.engine);

  SDL_DestroyRenderer(state.renderer);
  SDL_DestroyWindow(state.window);

  libraw_free_image(state.img);
}
