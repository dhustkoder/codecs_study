#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include "platform_layer.h"

static double rational_result(AVRational* r)
{
	return (double)r->num / (double)r->den;
}


void codecs_study_main(int argc, char** argv)
{
	int err;
	struct pl_buffer file;
	AVFormatContext *av_fmt_ctx;
	AVIOContext *av_io;
	AVStream* video_stream = NULL;
	AVCodecParameters* video_codecpar;
	AVCodec* video_codec = NULL;
	AVCodecContext* av_codec_ctx;
	AVFrame* av_frame;
	AVPacket* av_packet;

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
	log_info("width: %d", av_codec_ctx->width);
	log_info("height: %d", av_codec_ctx->height);


	av_frame = av_frame_alloc();
	assert(av_frame != NULL);

	av_packet = av_packet_alloc();
	assert(av_packet != NULL);

	const double time_base = rational_result(&video_stream->time_base);
	log_info("time_base: %.4lf", time_base);

	const tick_t start_ticks = pl_get_ticks();

	while (!pl_close_request()) {
		if (av_read_frame(av_fmt_ctx, av_packet) >= 0) {
			if (av_packet->stream_index != video_stream->index) {
				av_packet_unref(av_packet);
				continue;
			}

			err = avcodec_send_packet(av_codec_ctx, av_packet);
			assert(err == 0);
			
			err = avcodec_receive_frame(av_codec_ctx, av_frame);
			if (err != 0) {
				av_packet_unref(av_packet);
				continue;
			}

			log_info("frame pts: %lld", av_frame->pts);

			tick_t pts_ticks = (av_frame->pts * time_base) * PL_TICKS_PER_SEC;
			tick_t current_ticks = pl_get_ticks() - start_ticks;
			if (current_ticks < pts_ticks) {
				pl_sleep(pts_ticks - current_ticks);
			} else {
				av_frame_unref(av_frame);
				av_packet_unref(av_packet);
				continue;
			}

			pl_video_render_yuv(
				av_frame->data[0], av_frame->data[1], av_frame->data[2],
				av_frame->linesize[0], av_frame->linesize[1], av_frame->linesize[2]
			);

			av_frame_unref(av_frame);
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


