#include "platform_layer.h"

static SDL_Texture* texture = NULL;
static SDL_Renderer* renderer = NULL;
static SDL_Window* window = NULL;
 SDL_AudioDeviceID audio_device = 0;


static int video_w, video_h, video_bpp;
static int audio_freq, audio_channels, audio_bps;


static void init_sdl2(void)
{
	if (SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO) != 0) {
		log_error("Couldn't initialize SDL: %s\n", SDL_GetError());
		assert(false);
	}

	// video
	window = SDL_CreateWindow("CODECS_STUDY", SDL_WINDOWPOS_CENTERED,
				  SDL_WINDOWPOS_CENTERED,
				  PL_DEFAULT_SCR_W, PL_DEFAULT_SCR_H,
				  SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
	assert(window != NULL);

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	assert(renderer != NULL);

	pl_cfg_video(PL_DEFAULT_SCR_W, PL_DEFAULT_SCR_H, PL_DEFAULT_VIDEO_FMT);
	pl_cfg_audio(PL_DEFAULT_AUDIO_FREQ, PL_DEFAULT_AUDIO_CHANNELS, PL_DEFAULT_AUDIO_FMT);


	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);
}

static void term_sdl2(void)
{
	SDL_CloseAudioDevice(audio_device);
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}


void pl_sleep(int ms)
{
	SDL_Delay(ms);
}


void pl_read_file_ex(void*(*alloc_fn)(size_t size), const char* filepath, struct pl_buffer* plb)
{
	SDL_RWops* f = SDL_RWFromFile(filepath, "rb");
	assert(f != NULL);
	plb->size = f->size(f);
	plb->data = alloc_fn(plb->size);
	assert(plb->data != NULL);
	f->read(f, plb->data, plb->size, 1);
	f->close(f);
}

void pl_free_buffer_ex(void(*free_fn)(void* ptr), struct pl_buffer* plb)
{
	free_fn(plb->data);
}

bool pl_close_request(void)
{
	SDL_Event event;
	while (SDL_PollEvent(&event) != 0) {
		switch (event.type) {
		case SDL_QUIT:
			log_info("SDL QUIT");
			return true;
		}
	}

	return false;	
}


void pl_cfg_video(int w, int h, pl_video_fmt_t fmt)
{
	u32 sdl_fmt;

	assert(w > 0 && w <= 1920);
	assert(h > 0 && h <= 1080);

	switch (fmt) {
	case PL_VIDEO_FMT_RGB24:
		sdl_fmt = SDL_PIXELFORMAT_RGB24;
		video_bpp = 3;
		break;
	case PL_VIDEO_FMT_YUV:
		sdl_fmt = SDL_PIXELFORMAT_IYUV;
		video_bpp = 1;
		break;
	default:
		assert(false);
		break;
	}

	video_w = w;
	video_h = h;

	if (texture != NULL)
		SDL_DestroyTexture(texture);

	texture = SDL_CreateTexture(
		renderer,
		sdl_fmt,
		SDL_TEXTUREACCESS_STREAMING,
		video_w, video_h
	);

	assert(texture != NULL);
}

void pl_cfg_audio_ex(int freq, int channels, pl_audio_fmt_t fmt, audio_callback_fn_t callback)
{
	SDL_AudioFormat sdl_fmt;

	assert(
		freq > 1024 && 
		freq <= 48000 && 
		freq % 2 == 0 && 
		channels >= 1 && 
		channels <= 2
	);

	switch (fmt) {
	case PL_AUDIO_FMT_S16LE:
		sdl_fmt = AUDIO_S16LSB;
		audio_bps = 2;
		break;
	case PL_AUDIO_FMT_F32SYS:
		sdl_fmt = AUDIO_F32SYS;
		audio_bps = 4;
		break;
	default:
		assert(false);
		break;
	}

	audio_freq = freq;
	audio_channels = channels;

	if (audio_device != 0)
		SDL_CloseAudioDevice(audio_device);

	SDL_AudioSpec want;
	SDL_zero(want);
	want.freq = audio_freq;
	want.format = sdl_fmt;
	want.channels = audio_channels;
	want.samples = 1024;
	want.callback = callback;
	if ((audio_device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0)) == 0) {
		assert(false);
	}

	SDL_PauseAudioDevice(audio_device, 0);
}

void pl_video_render(void* data)
{
	int pitch;
	Uint32* pixels;
	SDL_LockTexture(texture, NULL, (void**)&pixels, &pitch);
	memcpy(pixels, data, video_w * video_h * video_bpp);
	SDL_UnlockTexture(texture);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

void pl_video_render_yuv(void* y, void* u, void* v, int ypitch, int upitch, int vpitch)
{
	SDL_UpdateYUVTexture(texture, NULL, y, ypitch, u, upitch, v, vpitch);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

void pl_audio_render_ex(void* data, size_t size)
{
	SDL_QueueAudio(audio_device, data, size);
}

void pl_audio_render(void* data)
{
	pl_audio_render_ex(data, audio_freq * audio_bps * audio_channels);
}


int main(int argc, char** argv)
{
	init_sdl2();

	codecs_study_main(argc, argv);

	term_sdl2();

	return 0;
}



