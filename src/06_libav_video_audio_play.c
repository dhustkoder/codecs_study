#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include "platform_layer.h"

#define FRAME_BUFFER_SIZE (64)

int audio_frames_cnt = 0;
int video_frames_cnt = 0;
AVFrame* audio_frames[FRAME_BUFFER_SIZE];
AVFrame* video_frames[FRAME_BUFFER_SIZE];



void codecs_study_main(int argc, char** argv)
{
	int err;
	struct pl_buffer file;
	
	AVFormatContext *av_fmt_ctx = NULL;
	AVIOContext *av_io = NULL;

	AVStream* video_stream = NULL;
	AVCodecParameters* video_codecpar = NULL;
	AVCodec* video_codec = NULL;
	AVCodecContext* video_codec_ctx = NULL;

	AVStream* audio_stream = NULL;
	AVCodecParameters* audio_codecpar = NULL;
	AVCodec* audio_codec = NULL;
	AVCodecContext* audio_codec_ctx = NULL;
	

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
		if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			video_stream = stream;
			video_codecpar = video_stream->codecpar;
		}
		if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audio_stream = stream;
			audio_codecpar = audio_stream->codecpar;
		}
	}

	assert(video_stream != NULL && video_codecpar != NULL);
	assert(audio_stream != NULL && audio_codecpar != NULL);

	// find codecs
	video_codec = avcodec_find_decoder(video_codecpar->codec_id);
	assert(video_codec != NULL);

	audio_codec = avcodec_find_decoder(audio_codecpar->codec_id);
	assert(audio_codec != NULL);

	// alloc codec ctxs
	video_codec_ctx = avcodec_alloc_context3(video_codec);
	assert(video_codec_ctx != NULL);

	audio_codec_ctx = avcodec_alloc_context3(audio_codec);
	assert(audio_codec_ctx != NULL);


	// set codec ctx params
	err = avcodec_parameters_to_context(video_codec_ctx, video_codecpar);
	assert(err == 0);

	err = avcodec_parameters_to_context(audio_codec_ctx, audio_codecpar);
	assert(err == 0);

	// open decoders ?
	err = avcodec_open2(video_codec_ctx, video_codec, NULL);
	assert(err == 0);
	err = avcodec_open2(audio_codec_ctx, audio_codec, NULL);
	assert(err == 0);
	
	pl_cfg_video(video_codec_ctx->width, video_codec_ctx->height, PL_VIDEO_FMT_YUV);
	pl_cfg_audio(audio_codec_ctx->sample_rate, audio_codec_ctx->channels, PL_AUDIO_FMT_F32SYS);
	channels_buffer = malloc(4 * audio_codec_ctx->channels * audio_codec_ctx->sample_rate);
	assert(channels_buffer != NULL);

	const AVRational vtime_base_rat = video_stream->time_base;
	const AVRational atime_base_rat = audio_stream->time_base;
	const double vtime_base = (double)vtime_base_rat.num / (double)vtime_base_rat.den;
	const double atime_base = (double)atime_base_rat.num / (double)atime_base_rat.den;


	log_info("width: %d", video_codec_ctx->width);
	log_info("height: %d", video_codec_ctx->height);
	log_info("vtime_base double: %.6lf", vtime_base);
	log_info("atime_base double: %.6lf", atime_base);


	log_info("AUDIO FREQUENCY: %d", audio_codec_ctx->sample_rate);
	log_info("CHANNELS: %d", audio_codec_ctx->channels);
	log_info("SAMPLE FMT: %s", av_get_sample_fmt_name(audio_codec_ctx->sample_fmt));
	log_info("FRAME SIZE: %d", audio_codec_ctx->frame_size);
	log_info("initial padding: %d", audio_codec_ctx->initial_padding);
	log_info("trailing padding: %d", audio_codec_ctx->trailing_padding);

	av_frame = av_frame_alloc();
	assert(av_frame != NULL);

	av_packet = av_packet_alloc();
	assert(av_packet != NULL);

	const tick_t start_ticks = pl_get_ticks();

	while (!pl_close_request()) {

		if (video_frames_cnt < FRAME_BUFFER_SIZE && audio_frames_cnt < FRAME_BUFFER_SIZE) {
			if (av_read_frame(av_fmt_ctx, av_packet) >= 0) {
				AVCodecContext* av_codec_ctx;

				if (av_packet->stream_index == video_stream->index) {
					av_codec_ctx = video_codec_ctx;
				} else if (av_packet->stream_index == audio_stream->index) {
					av_codec_ctx = audio_codec_ctx;
				}  else {
					goto Lunref_packet;
				}

				err = avcodec_send_packet(av_codec_ctx, av_packet);
				assert(err == 0);
				
				err = avcodec_receive_frame(av_codec_ctx, av_frame);
				if (err != 0)
					goto Lunref_packet;

				if (av_packet->stream_index == video_stream->index) {
					video_frames[video_frames_cnt++] = av_frame_clone(av_frame);
				} else {
					audio_frames[audio_frames_cnt++] = av_frame_clone(av_frame);
				}

				av_frame_unref(av_frame);
				Lunref_packet:
				av_packet_unref(av_packet);
			}
		} 

		AVFrame** video_frame_itr = &video_frames[0];
		AVFrame** audio_frame_itr = &audio_frames[0];
		while (video_frames_cnt > 0 && audio_frames_cnt > 0) {

			AVFrame* rendering_frame;
			double frame_pts;
			
			if (*video_frame_itr == NULL && *audio_frame_itr == NULL) {
				video_frames_cnt = 0;
				audio_frames_cnt = 0;
				continue;
			}

			if (*video_frame_itr != NULL && *audio_frame_itr == NULL) {
				rendering_frame = *video_frame_itr;
			} else if (*audio_frame_itr != NULL && *video_frame_itr == NULL) {
				rendering_frame = *audio_frame_itr;
			} else {
				const double video_pts = (*video_frame_itr)->pts * vtime_base;
				const double audio_pts = (*audio_frame_itr)->pts * atime_base;
				if (video_pts < audio_pts) {
					frame_pts = video_pts;
					rendering_frame = *video_frame_itr;
				} else {
					frame_pts = audio_pts;
					rendering_frame = *audio_frame_itr;
				}
			}

			if (rendering_frame->pts <= 0)
				goto Lunref_rendering_frame;

			double current_ticks = ((double)pl_get_ticks() - (double)start_ticks) / PL_TICKS_PER_SEC;

			log_info("%s FRAME INFO: ", rendering_frame == *video_frame_itr ? "VIDEO" : "AUDIO");
			log_info("frame pts: %" PRId64, rendering_frame->pts);
			log_info("frame pts * timebase: %.6lf", frame_pts);
			log_info("current_ticks: %.6lf", current_ticks);

			if (current_ticks < frame_pts)
				pl_sleep(((frame_pts - current_ticks)/2) * PL_TICKS_PER_SEC);


			if (rendering_frame == *video_frame_itr) {
				pl_video_render_yuv(
					rendering_frame->data[0], rendering_frame->data[1], rendering_frame->data[2],
					rendering_frame->linesize[0], rendering_frame->linesize[1], rendering_frame->linesize[2]
				);
			} else {
				assert(rendering_frame->channels < AV_NUM_DATA_POINTERS);
				for (int i = 0; i < rendering_frame->nb_samples; ++i) {
					for (int c = 0; c < rendering_frame->channels; ++c) {
		             	float* extended_data = (float*)rendering_frame->extended_data[c];
		             	channels_buffer[(i*rendering_frame->channels) + c] = extended_data[i];
					}
				}
				pl_audio_render_ex(channels_buffer, rendering_frame->nb_samples * rendering_frame->channels * 4);
			}

			Lunref_rendering_frame:
			if (rendering_frame == *video_frame_itr) {
				*video_frame_itr = NULL;
				++video_frame_itr;
			} else {
				*audio_frame_itr = NULL;
				++audio_frame_itr;
			}

			av_frame_unref(rendering_frame);
		}
	}

	free(channels_buffer);
	av_frame_free(&av_frame);
	av_packet_free(&av_packet);
	avcodec_free_context(&video_codec_ctx);
	avcodec_free_context(&audio_codec_ctx);
	avformat_free_context(av_fmt_ctx);
	avio_context_free(&av_io);
	pl_free_buffer_ex(av_free, &file);
}


