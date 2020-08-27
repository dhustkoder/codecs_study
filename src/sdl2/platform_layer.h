#ifndef DVS_PLATFORM_LAYER_H_
#define DVS_PLATFORM_LAYER_H_
#include "base_defs.h"

#define PL_SCR_W (1280)
#define PL_SCR_H (720)

#define log_info(...)  SDL_Log(__VA_ARGS__)
#define log_error(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#ifdef WLU_DEBUG
#define log_debug(...) SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#else
#define log_debug(...) ((void)0)
#endif



extern void dvs_main(void);
extern void pl_render_buffer(void* data);
extern bool pl_close_request(void);







#endif






