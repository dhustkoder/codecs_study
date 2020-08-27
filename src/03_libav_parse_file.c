#include <libavformat/avformat.h>

#include "platform_layer.h"





void codecs_study_main(int argc, char** argv)
{
	int err;
	struct pl_buffer file;
	AVFormatContext *avfmt;
	AVIOContext *avio;

	assert(argc == 2);

	pl_read_file_ex(av_malloc, argv[1], &file);

	avio = avio_alloc_context(
		file.data,
		file.size,
		0,
		NULL, 
		NULL,
		NULL,
		NULL
	);

	assert(avio != NULL);

	// avio_alloc_context frees the buffer
	file.data = NULL; 
	file.size = 0;

	avfmt = avformat_alloc_context();
	assert(avfmt != NULL);
	
	avfmt->pb = avio;
	err = avformat_open_input(&avfmt, "", NULL, NULL);
	assert(err == 0);

	for (unsigned i = 0; i < avfmt->nb_streams; ++i) {
		AVStream* stream = avfmt->streams[i];
		log_info("\n"
			"STREAM %d\n"
			"TYPE: %s\n"
			"CODEC: %s\n",
			i + 1,
			av_get_media_type_string(stream->codecpar->codec_type),
			avcodec_get_name(stream->codecpar->codec_id)
		);
	}

	avformat_free_context(avfmt);
	avio_context_free(&avio);
	pl_free_buffer_ex(av_free, &file);
}


