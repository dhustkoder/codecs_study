#include "platform_layer.h"




// usage: ./app audiofile.pcm audiofrequency channels audiofmt
// example: ./app smbtheme.pcm 1600 1 s16le


void dvs_main(int argc, char** argv)
{
	struct pl_buffer f;
	pl_audio_fmt_t fmt;
	int freq, channels, bps;

	assert(argc == 5);
	pl_read_file(argv[1], &f);

	freq = atoi(argv[2]);
	channels = atoi(argv[3]);

	assert(freq > 1024 && freq <= 48000 && freq % 2 == 0);

	if (strcmp(argv[4], "s16le") == 0) {
		fmt = PL_AUDIO_FMT_S16LE;
		bps = 2;
	} else {
		assert(false);
	}

	log_info("file size: %llu", f.size);

	const u64 sample_queue_size = bps * freq * channels;

	u8* itr = f.data;
	assert(f.size >= sample_queue_size);


	pl_cfg_audio(freq, channels, fmt);

	while (!pl_close_request()) {
		const u64 size_read = itr - f.data;

		if (size_read < f.size && (f.size - size_read) >= sample_queue_size) {
			pl_audio_render(itr);
			itr += sample_queue_size;
		} else {
			itr = f.data;
		}

		pl_sleep(1000);
	}

	pl_free_buffer(&f);
}


