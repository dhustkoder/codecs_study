#include "platform_layer.h"


// usage: ./app videofile.raw width height fps videofmt
// example: ./app kitty.raw 1280 720 22 rgb24


void dvs_main(int argc, char** argv)
{
	pl_video_fmt_t vfmt;
	struct pl_buffer f;
	int width, height, bpp, fps;

	assert(argc == 6);
	
	pl_read_file(argv[1], &f);

	width = atoi(argv[2]);
	height = atoi(argv[3]);
	fps = atoi(argv[4]);

	assert(width > 0 && width <= 1280);
	assert(height > 0 && height <= 720);

	if (strcmp(argv[5], "rgb24") == 0) {
		vfmt = PL_VIDEO_FMT_RGB24;
		bpp = 3;
	} else {
		assert(false);
	}

	log_info("file size: %llu", f.size);
	log_info("width: %d", width);
	log_info("height: %d", height);
	log_info("fps: %d", fps);
	log_info("fmt: %s", argv[5]);

	const u64 frame_size = width * height * bpp;

	u8* itr = f.data;
	assert(f.size >= frame_size);

	pl_cfg_video(width, height, vfmt);

	while (!pl_close_request()) {
		const u64 size_read = itr - f.data;

		if (size_read < f.size && (f.size - size_read) >= frame_size) {
			itr += frame_size;
		} else {
			itr = f.data;
		}

		pl_video_render(itr);
		pl_sleep(1000 / fps);
	}

	pl_free_buffer(&f);
}


