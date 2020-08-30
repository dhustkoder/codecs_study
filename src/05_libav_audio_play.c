#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include "platform_layer.h"

static double rational_result(AVRational* r)
{
	return (double)r->num / (double)r->den;
}


u8 buffer[3840 * 2];


void codecs_study_main(int argc, char** argv)
{
	int err;
	struct pl_buffer file;
	
	AVFormatContext *av_fmt_ctx = NULL;
	AVIOContext *av_io = NULL;

	AVStream* audio_stream = NULL;
	AVCodecParameters* audio_codecpar = NULL;
	
	AVCodec* audio_codec = NULL;
	AVCodecContext* av_codec_ctx = NULL;
	
	AVFrame* av_frame = NULL;
	AVPacket* av_packet = NULL;


	assert(argc == 2);

	pl_read_file_ex(av_malloc, argv[1], &file);

	av_io = avio_alloc_context(
		file.data,
		file.size,
		0,
		NULL, 
		NULL,
		NULL,
		NULL
	);

	assert(av_io != NULL);

	// avio_alloc_context frees the buffer
	file.data = NULL; 
	file.size = 0;

	av_fmt_ctx = avformat_alloc_context();
	assert(av_fmt_ctx != NULL);
	
	av_fmt_ctx->pb = av_io;
	err = avformat_open_input(&av_fmt_ctx, "", NULL, NULL);
	assert(err == 0);

	for (unsigned i = 0; i < av_fmt_ctx->nb_streams; ++i) {
		AVStream* stream = av_fmt_ctx->streams[i];
		if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audio_stream = stream;
			audio_codecpar = audio_stream->codecpar;
			break;
		}
	}
	assert(audio_stream != NULL && audio_codecpar != NULL);

	audio_codec = avcodec_find_decoder(audio_codecpar->codec_id);
	assert(audio_codec != NULL);

	av_codec_ctx = avcodec_alloc_context3(audio_codec);
	assert(av_codec_ctx != NULL);

	err = avcodec_parameters_to_context(av_codec_ctx, audio_codecpar);
	assert(err == 0);

	err = avcodec_open2(av_codec_ctx, audio_codec, NULL);
	assert(err == 0);

	
	// config audio
	log_info("AUDIO FREQUENCY: %d", av_codec_ctx->sample_rate);
	log_info("CHANNELS: %d", av_codec_ctx->channels);
	log_info("SAMPLE FMT: %s", av_get_sample_fmt_name(av_codec_ctx->sample_fmt));
	log_info("initial padding: %d", av_codec_ctx->initial_padding);
	log_info("trailing padding: %d", av_codec_ctx->trailing_padding);

	pl_cfg_audio(av_codec_ctx->sample_rate, av_codec_ctx->channels, PL_AUDIO_FMT_F32SYS);

	av_frame = av_frame_alloc();
	assert(av_frame != NULL);

	av_packet = av_packet_alloc();
	assert(av_packet != NULL);

	const double time_base = rational_result(&audio_stream->time_base);
	log_info("time_base: %.4lf", time_base);

	const tick_t start_ticks = pl_get_ticks();

	while (!pl_close_request()) {
		if (av_read_frame(av_fmt_ctx, av_packet) >= 0) {
			if (av_packet->stream_index != audio_stream->index)
				goto Lunref_packet;

			err = avcodec_send_packet(av_codec_ctx, av_packet);
			assert(err == 0);
			
			err = avcodec_receive_frame(av_codec_ctx, av_frame);
			if (err != 0)
				goto Lunref_packet;

			log_info("pts: %ld", av_frame->pts);
			log_info("nb_samples: %d", av_frame->nb_samples);
			log_info("linesize: %d", av_frame->linesize[0]);

			if (av_frame->pts < 0)
				goto Lunref_frame;

			tick_t pts_ticks = (av_frame->pts * time_base) * PL_TICKS_PER_SEC;
			tick_t current_ticks = pl_get_ticks() - start_ticks;
			if (current_ticks < pts_ticks) {
				pl_sleep(pts_ticks - current_ticks);
			}

			// render audio

			assert(av_frame->channels < AV_NUM_DATA_POINTERS);
			pl_audio_render_ex(av_frame->data[0], av_frame->linesize[0]);
			pl_audio_render_ex(av_frame->data[1], av_frame->linesize[0]);
			

			Lunref_frame:
			av_frame_unref(av_frame);
			Lunref_packet:
			av_packet_unref(av_packet);
		}
	}

	av_frame_free(&av_frame);
	av_packet_free(&av_packet);
	avcodec_free_context(&av_codec_ctx);
	avformat_free_context(av_fmt_ctx);
	avio_context_free(&av_io);
	pl_free_buffer_ex(av_free, &file);
}


