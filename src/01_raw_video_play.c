#include "platform_layer.h"


// usage: ./app videofile.raw width height fps videofmt
// example: ./app kitty.raw 1280 720 22 rgb24


void codecs_study_main(int argc, char** argv)
{
	pl_video_fmt_t vfmt;
	struct pl_buffer file;
	int width, height, bpp, fps;

	assert(argc == 6);
	
	pl_read_file(argv[1], &file);

	width = atoi(argv[2]);
	height = atoi(argv[3]);
	fps = atoi(argv[4]);

	if (strcmp(argv[5], "rgb24") == 0) {
		vfmt = PL_VIDEO_FMT_RGB24;
		bpp = 3;
	} else {
		assert(false);
	}

	pl_cfg_video(width, height, vfmt);

	log_info("file size: %llu", file.size);
	log_info("width: %d", width);
	log_info("height: %d", height);
	log_info("fps: %d", fps);
	log_info("fmt: %s", argv[5]);

	const u64 frame_size = width * height * bpp;
	assert(file.size >= frame_size);

	u8* itr = file.data;
	while (!pl_close_request()) {
		const u64 size_read = itr - file.data;

		if (size_read < file.size && (file.size - size_read) >= frame_size) {
			itr += frame_size;
		} else {
			itr = file.data;
		}

		pl_video_render(itr);
		pl_sleep(1000 / fps);
	}

	pl_free_buffer(&file);
}


