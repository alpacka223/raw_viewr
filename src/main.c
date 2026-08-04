#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>

#include <libraw/libraw.h>

#include <stdio.h>

static struct {
  char *filename;
  bool is_dropping_file;

  int dt;
} ui_state;

void draw_image() {}

void draw_ui() {}

int main(void) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    fprintf(stderr, "SDL Init Failed: %s", SDL_GetError());
    return -1;
  }

  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;

  if (!SDL_CreateWindowAndRenderer("RAWR", 800, 600,
                                   SDL_WINDOW_RESIZABLE |
                                       SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                   &window, &renderer)) {
    fprintf(stderr, "Window/Renderer Creation Failed: %s", SDL_GetError());
    SDL_Quit();
    return -1;
  }

  bool isRunning = true;
  SDL_Event event;
  while (isRunning) {
    while (SDL_PollEvent(&event)) {
      switch (event.type) {

      case SDL_EVENT_QUIT:
        isRunning = false;
        break;

      case SDL_EVENT_DROP_BEGIN:
        // should start drawing stuff
        break;

      case SDL_EVENT_DROP_FILE:
        break;

      case SDL_EVENT_MOUSE_BUTTON_UP:
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        break;

      case SDL_EVENT_KEY_UP:
      case SDL_EVENT_KEY_DOWN:
        break;

      case SDL_EVENT_WINDOW_RESIZED:
        break;
      }
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    draw_image();
    draw_ui();

    SDL_RenderPresent(renderer);
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}
