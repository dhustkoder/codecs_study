#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include "platform_layer.h"


void codecs_study_main(int argc, char** argv)
{
	int err;
	struct pl_buffer file;
	
	AVIOContext *av_io = NULL;
	AVFormatContext *av_fmt_ctx = NULL;

	AVStream* audio_stream = NULL;
	AVCodecParameters* audio_codecpar = NULL;
	
	AVCodec* audio_codec = NULL;
	AVCodecContext* av_codec_ctx = NULL;
	
	AVFrame* av_frame = NULL;
	AVPacket* av_packet = NULL;

	float* channels_buffer = NULL;


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
	pl_cfg_audio(av_codec_ctx->sample_rate, av_codec_ctx->channels, PL_AUDIO_FMT_F32SYS);
	channels_buffer = malloc(4 * av_codec_ctx->channels * av_codec_ctx->sample_rate);
	assert(channels_buffer != NULL);

	const double time_base = av_q2d(audio_stream->time_base);

	log_info("AUDIO FREQUENCY: %d", av_codec_ctx->sample_rate);
	log_info("CHANNELS: %d", av_codec_ctx->channels);
	log_info("SAMPLE FMT: %s", av_get_sample_fmt_name(av_codec_ctx->sample_fmt));
	log_info("FRAME SIZE: %d", av_codec_ctx->frame_size);
	log_info("initial padding: %d", av_codec_ctx->initial_padding);
	log_info("trailing padding: %d", av_codec_ctx->trailing_padding);
	log_info("time_base double: %.6lf", time_base);

	av_frame = av_frame_alloc();
	assert(av_frame != NULL);

	av_packet = av_packet_alloc();
	assert(av_packet != NULL);

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

			const double pts_ticks = (av_frame->pts * time_base);
			const double current_ticks = ((double)pl_get_ticks() - (double)start_ticks) / PL_TICKS_PER_SEC;

			log_info("pts: %" PRId64, av_frame->pts);
			log_info("frame pts * timebase: %.6lf", pts_ticks);
			log_info("current_ticks: %.6lf", current_ticks);
			log_info("nb_samples: %d", av_frame->nb_samples);
			log_info("linesize: %d", av_frame->linesize[0]);

			if (current_ticks < pts_ticks) {
				pl_sleep((pts_ticks - current_ticks) * PL_TICKS_PER_SEC);
			}


			assert(av_frame->channels < AV_NUM_DATA_POINTERS);
			for (int i = 0; i < av_frame->nb_samples; ++i) {
				for (int c = 0; c < av_frame->channels; ++c) {
					float* extended_data = (float*)av_frame->extended_data[c];
					channels_buffer[(i*av_frame->channels) + c] = extended_data[i];
					// pl_audio_render_ex(&extended_data[i], sizeof(float));
				}
			}

			pl_audio_render_ex(channels_buffer, av_frame->nb_samples * av_frame->channels * 4);

			Lunref_frame:
			av_frame_unref(av_frame);
			Lunref_packet:
			av_packet_unref(av_packet);
		}
	}

	free(channels_buffer);
	av_frame_free(&av_frame);
	av_packet_free(&av_packet);
	avcodec_close(av_codec_ctx);
	avcodec_free_context(&av_codec_ctx);
	avformat_close_input(&av_fmt_ctx);
	avformat_free_context(av_fmt_ctx);
	av_free(av_io->buffer);
	av_free(av_io);
	pl_free_buffer_ex(av_free, &file);
}


