#ifndef CODECS_STUDY_PLATFORM_LAYER_H_
#define CODECS_STUDY_PLATFORM_LAYER_H_
#include "base_defs.h"

#define PL_DEFAULT_SCR_W       (640)
#define PL_DEFAULT_SCR_H       (360)
#define PL_DEFAULT_VIDEO_FMT   PL_VIDEO_FMT_RGB24

#define PL_DEFAULT_AUDIO_FMT   PL_AUDIO_FMT_S16LE
#define PL_DEFAULT_AUDIO_CHANNELS 2
#define PL_DEFAULT_AUDIO_FREQ  (16000)


#define log_info(...)  SDL_Log(__VA_ARGS__)
#define log_error(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#ifdef WLU_DEBUG
#define log_debug(...) SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#else
#define log_debug(...) ((void)0)
#endif


enum pl_video_fmt {
	PL_VIDEO_FMT_RGB24,
	PL_VIDEO_FMT_YUV
};
typedef enum pl_video_fmt pl_video_fmt_t;


enum pl_audio_fmt {
	PL_AUDIO_FMT_S16LE
};
typedef enum pl_audio_fmt pl_audio_fmt_t;


struct pl_buffer {
	u64 size;
	u8* data;
};



#define PL_TICKS_PER_SEC 1000
#define pl_get_ticks() SDL_GetTicks()
typedef u64 tick_t;


extern void codecs_study_main(int argc, char** argv);

extern void pl_cfg_video(int w, int h, pl_video_fmt_t fmt);
extern void pl_cfg_audio(int freq, int channels, pl_audio_fmt_t fmt);

extern void pl_video_render(void* data);
extern void pl_video_render_yuv(void* y, void* u, void* v, int ypitch, int upitch, int vpitch);
extern void pl_audio_render(void* data);

extern bool pl_close_request(void);


extern void pl_read_file_ex(
	void*(*alloc_fn)(size_t size),
	const char* filepath,
	struct pl_buffer* plb
);
#define pl_read_file(...) pl_read_file_ex(SDL_malloc, __VA_ARGS__)


extern void pl_free_buffer_ex(void(*free_fn)(void* ptr), struct pl_buffer* plb);
#define pl_free_buffer(...) pl_free_buffer_ex(SDL_free, __VA_ARGS__)

extern void pl_sleep(int ms);




#endif






