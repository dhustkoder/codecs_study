#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include "platform_layer.h"


void codecs_study_main(int argc, char** argv)
{
	int err;
	struct pl_buffer file;
	AVFormatContext *av_fmt_ctx;
	AVIOContext *av_io;
	AVStream* video_stream = NULL;
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
			break;
		}
	}
	assert(video_stream != NULL);


	video_codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
	assert(video_codec != NULL);

	av_codec_ctx = avcodec_alloc_context3(video_codec);
	assert(av_codec_ctx != NULL);

	err = avcodec_parameters_to_context(av_codec_ctx, video_stream->codecpar);
	assert(err == 0);

	err = avcodec_open2(av_codec_ctx, video_codec, NULL);
	assert(err == 0);


	av_frame = av_frame_alloc();
	assert(av_frame != NULL);

	av_packet = av_packet_alloc();
	assert(av_packet != NULL);

	bool first_frame = true;

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
			
			if (first_frame) {
				log_info("FORMAT: %s", av_get_pix_fmt_name(av_frame->format));
				log_info("width: %d", av_frame->width);
				log_info("height: %d", av_frame->height);
				log_info("linesize: %d", av_frame->linesize[0]);
				log_info("linesize: %d", av_frame->linesize[1]);
				log_info("linesize: %d", av_frame->linesize[2]);
				switch (av_frame->format) {
				case AV_PIX_FMT_YUV420P:
					pl_cfg_video(av_frame->width, av_frame->height, PL_VIDEO_FMT_YUV);
					break;
				default:
					assert(false);
					break;
				}
				first_frame = false;
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


