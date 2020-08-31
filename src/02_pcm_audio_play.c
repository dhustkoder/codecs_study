#include "platform_layer.h"




// usage: ./app audiofile.pcm audiofrequency channels audiofmt
// example: ./app smbtheme.pcm 1600 1 s16le


void codecs_study_main(int argc, char** argv)
{
	struct pl_buffer file;
	pl_audio_fmt_t fmt;
	int freq, channels, bps;

	assert(argc == 5);

	pl_read_file(argv[1], &file);

	freq = atoi(argv[2]);
	channels = atoi(argv[3]);

	if (strcmp(argv[4], "s16le") == 0) {
		fmt = PL_AUDIO_FMT_S16LE;
		bps = 2;
	} else if (strcmp(argv[4], "f32") == 0) {
		fmt = PL_AUDIO_FMT_F32SYS;
		bps = 4;
	} else {
		assert(false);
	}

	pl_cfg_audio(freq, channels, fmt);


	log_info("file size: %llu", file.size);
	log_info("audio frequency: %d", freq);
	log_info("audio channels: %d", channels);
	log_info("audio fmt: %s", argv[4]);
	log_info("audio bps: %d", bps);

	const u64 frame_size = bps * freq * channels;
	assert(file.size >= frame_size);


	u8* itr = file.data;
	while (!pl_close_request()) {
		const u64 size_read = itr - file.data;
		const u64 size_left = file.size - size_read;

		if (size_read < file.size && size_left >= frame_size) {
			pl_audio_render(itr);
			itr += frame_size;
		} else {
			itr = file.data;
		}

		pl_sleep(1000);
	}

	pl_free_buffer(&file);
}


