#include "platform_layer.h"

static SDL_Texture* texture;
static SDL_Renderer* renderer;
static SDL_Window* window;

static void pl_init(void)
{
	if (SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO) != 0) {
		log_error("Couldn't initialize SDL: %s\n", SDL_GetError());
		assert(false);
	}

	// video
	window = SDL_CreateWindow("DVS", SDL_WINDOWPOS_CENTERED,
				  SDL_WINDOWPOS_CENTERED,
				  PL_SCR_W, PL_SCR_H,
				  SDL_WINDOW_SHOWN);
	assert(window != NULL);

	renderer = SDL_CreateRenderer(window, -1,
	                              SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
	assert(renderer != NULL);


	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
	                            SDL_TEXTUREACCESS_STREAMING,
	                            PL_SCR_W, PL_SCR_H);
	assert(texture != NULL);

	/*
	SDL_AudioSpec want;
	SDL_zero(want);
	want.freq = AUDIO_FREQUENCY;
	want.format = AUDIO_S16SYS;
	want.channels = 1;
	want.samples = AUDIO_BUFFER_SIZE;
	if ((sdl_audio_device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0)) == 0) {
		log_error("Failed to open audio: %s\n", SDL_GetError());
		goto Lfreetexture;
	}
	audio
	*/

	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);
	// SDL_PauseAudioDevice(sdl_audio_device, 0);
}

static void pl_term(void)
{
	// SDL_CloseAudioDevice(sdl_audio_device);
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}



void pl_read_file(const char* filepath, struct pl_buffer* plb)
{
	SDL_RWops* f = SDL_RWFromFile(filepath, "rb");
	assert(f != NULL);
	plb->size = f->size(f);
	plb->data = malloc(plb->size);
	assert(plb->data != NULL);
	f->read(f, plb->data, plb->size, 1);
	f->close(f);
}

void pl_free_buffer(struct pl_buffer* plb)
{
	free(plb->data);
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

void pl_render_buffer(void* data)
{
	int pitch;
	Uint32* pixels;
	SDL_LockTexture(texture, NULL, (void**)&pixels, &pitch);
	memcpy(pixels, data, PL_SCR_H * PL_SCR_W * 3);
	SDL_UnlockTexture(texture);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}


int main(int argc, char** argv)
{
	pl_init();

	dvs_main(argc, argv);

	pl_term();

	return 0;
}



