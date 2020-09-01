#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include "platform_layer.h"


void codecs_study_main(int argc, char** argv)
{
	int err;
	struct pl_buffer file;
	
	AVIOContext *av_io = NULL;
	AVFormatContext *av_fmt_ctx = NULL;

	AVStream* video_stream = NULL;
	AVCodecParameters* video_codecpar = NULL;
	
	AVCodec* video_codec = NULL;
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
		if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			video_stream = stream;
			video_codecpar = video_stream->codecpar;
			break;
		}
	}
	assert(video_stream != NULL && video_codecpar != NULL);

	video_codec = avcodec_find_decoder(video_codecpar->codec_id);
	assert(video_codec != NULL);

	av_codec_ctx = avcodec_alloc_context3(video_codec);
	assert(av_codec_ctx != NULL);

	err = avcodec_parameters_to_context(av_codec_ctx, video_codecpar);
	assert(err == 0);

	err = avcodec_open2(av_codec_ctx, video_codec, NULL);
	assert(err == 0);
	
	pl_cfg_video(av_codec_ctx->width, av_codec_ctx->height, PL_VIDEO_FMT_YUV);

	const AVRational time_base_rat = video_stream->time_base;
	const double time_base = (double)time_base_rat.num / (double)time_base_rat.den;

	log_info("width: %d", av_codec_ctx->width);
	log_info("height: %d", av_codec_ctx->height);
	log_info("time_base: num %d, den %d", time_base_rat.num, time_base_rat.den);
	log_info("time_base double: %.6lf", time_base);

	av_packet = av_packet_alloc();
	assert(av_packet != NULL);

	av_frame = av_frame_alloc();
	assert(av_frame != NULL);

	const tick_t start_ticks = pl_get_ticks();

	while (!pl_close_request()) {
		if (av_read_frame(av_fmt_ctx, av_packet) >= 0) {
			if (av_packet->stream_index != video_stream->index)
				goto Lunref_packet;

			err = avcodec_send_packet(av_codec_ctx, av_packet);
			assert(err == 0);
			
			err = avcodec_receive_frame(av_codec_ctx, av_frame);
			if (err != 0)
				goto Lunref_frame;


			const double pts_ticks = (av_frame->pts * time_base);
			const double current_ticks = ((double)pl_get_ticks() - (double)start_ticks) / PL_TICKS_PER_SEC;

			log_info("frame pts: %" PRId64, av_frame->pts);
			log_info("frame pts * timebase: %.6lf", pts_ticks);
			log_info("current_ticks: %.6lf", current_ticks);

			if (current_ticks < pts_ticks) {
				pl_sleep((pts_ticks - current_ticks) * PL_TICKS_PER_SEC);
			}

			pl_video_render_yuv(
				av_frame->data[0], av_frame->data[1], av_frame->data[2],
				av_frame->linesize[0], av_frame->linesize[1], av_frame->linesize[2]
			);

			Lunref_frame:
			av_frame_unref(av_frame);
			Lunref_packet:
			av_packet_unref(av_packet);
		}
	}

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


