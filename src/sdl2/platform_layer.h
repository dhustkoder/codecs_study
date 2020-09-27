#ifndef CODECS_STUDY_PLATFORM_LAYER_H_
#define CODECS_STUDY_PLATFORM_LAYER_H_
#include <SDL2/SDL.h>
#include <SDL2/SDL_net.h>

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
	PL_AUDIO_FMT_S16LE,
	PL_AUDIO_FMT_F32SYS
};
typedef enum pl_audio_fmt pl_audio_fmt_t;


struct pl_buffer {
	size_t size;
	u8* data;
};

typedef void (*audio_callback_fn_t)(void* userdata, u8* stream, int len);


#define PL_TICKS_PER_SEC 1000
#define pl_get_ticks() SDL_GetTicks()
typedef u64 tick_t;



typedef UDPsocket pl_udp_socket_t;



extern void codecs_study_main(int argc, char** argv);

extern void pl_cfg_video(int w, int h, pl_video_fmt_t fmt);
extern void pl_cfg_audio_ex(int freq, int channels, pl_audio_fmt_t fmt, audio_callback_fn_t callback);
#define pl_cfg_audio(freq, channels, fmt) pl_cfg_audio_ex(freq, channels, fmt, NULL)

extern void pl_video_render(void* data);
extern void pl_video_render_yuv(void* y, void* u, void* v, int ypitch, int upitch, int vpitch);
extern void pl_audio_render_ex(void* data, size_t size);
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

extern pl_udp_socket_t pl_socket_udp_sender_create(const char* ip, u16 port);
extern pl_udp_socket_t pl_socket_udp_receiver_create(u16 port);
extern void pl_socket_udp_send(pl_udp_socket_t socket, void* data, size_t size);
extern void pl_socket_udp_recv(pl_udp_socket_t socket, void* data, size_t size);

#endif






