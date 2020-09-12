#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include "platform_layer.h"

#define FRAME_QUEUE_SIZE (2048)

struct frame_queue {
	AVFrame* frames[FRAME_QUEUE_SIZE];
	AVFrame** in_itr;
	AVFrame** out_itr;
	int cnt;
};

static struct frame_queue video_queue;
static struct frame_queue audio_queue;


static void frame_queue_init(struct frame_queue* fq)
{
	memset(fq->frames, 0, FRAME_QUEUE_SIZE * sizeof(*fq->frames));
	fq->in_itr = fq->frames;
	fq->out_itr = fq->frames;
	fq->cnt = 0;
}


static void frame_queue_add(struct frame_queue* fq, AVFrame* src)
{
	assert(fq->cnt < FRAME_QUEUE_SIZE);

	*fq->in_itr = av_frame_clone(src);
	++fq->in_itr;
	
	if ((fq->in_itr - fq->frames) == FRAME_QUEUE_SIZE)
		fq->in_itr = fq->frames;
	
	++fq->cnt;
} 


static void frame_queue_rem(struct frame_queue* fq, AVFrame** dest)
{
	assert(*fq->out_itr != NULL);
	*dest = *fq->out_itr;
	*fq->out_itr = NULL;
	++fq->out_itr;
	
	if ((fq->out_itr - fq->frames) == FRAME_QUEUE_SIZE)
		fq->out_itr = fq->frames;

	--fq->cnt;
}


AVFormatContext *av_fmt_ctx = NULL;
AVStream* audio_stream = NULL;
AVStream* video_stream = NULL;
AVCodecContext* audio_codec_ctx = NULL;
AVCodecContext* video_codec_ctx = NULL;

AVFrame* av_frame = NULL;
AVPacket* av_packet = NULL;
float* channels_buffer = NULL;

double atime_base;
double vtime_base;
double playing_audio_pts = 0;


static void decode_and_queue_frame(void)
{
	int err;
	if (av_read_frame(av_fmt_ctx, av_packet) >= 0) {
		
		AVCodecContext* cctx;

		if (av_packet->stream_index == audio_stream->index) {
			cctx = audio_codec_ctx;
		} else if (av_packet->stream_index == video_stream->index) {
			cctx = video_codec_ctx;
		} else {
			goto Lunref_packet;
		}

		err = avcodec_send_packet(cctx, av_packet);
		assert(err == 0);
		
		err = avcodec_receive_frame(cctx, av_frame);
		if (err != 0)
			goto Lunref_packet;


		if (cctx == video_codec_ctx)
			frame_queue_add(&video_queue, av_frame);
		else
			frame_queue_add(&audio_queue, av_frame);

		av_frame_unref(av_frame);
		Lunref_packet:
		av_packet_unref(av_packet);
	}

}

static void play_video(void)
{
	static tick_t start_ticks = 0;
	static AVFrame* frame = NULL;
	static int audio_frames_queued = 0;

	extern SDL_AudioDeviceID audio_device;
	
	if (audio_frames_queued != (SDL_GetQueuedAudioSize(audio_device) / 8192)) {
		audio_frames_queued = (SDL_GetQueuedAudioSize(audio_device) / 8192);
		log_info("QUEUED AUDIO SIZE: %u", audio_frames_queued);
	}

	if (start_ticks == 0) {
		start_ticks = pl_get_ticks();
	}

	if (frame == NULL) {
		if (video_queue.cnt > 0)
			frame_queue_rem(&video_queue, &frame);
		else
			return;
	}

	const double pts_ticks = frame->pts  * vtime_base;
	const double current_ticks = ((double)pl_get_ticks() - (double)start_ticks) / PL_TICKS_PER_SEC;

	if ((current_ticks - 2.250) > pts_ticks) {
		pl_video_render_yuv(
			frame->data[0], frame->data[1], frame->data[2],
			frame->linesize[0], frame->linesize[1], frame->linesize[2]
		);
		av_frame_unref(frame);
		frame = NULL;
	}

}



static void play_audio(void)
{
	static tick_t start_ticks = 0;
	static AVFrame* frame = NULL;

	if (start_ticks == 0) {
		start_ticks = pl_get_ticks();
	}

	if (frame == NULL) {
		if (audio_queue.cnt > 0)
			frame_queue_rem(&audio_queue, &frame);
		else
			return;
	}

	const double pts_ticks = frame->pts * atime_base;
	const double current_ticks = ((double)pl_get_ticks() - (double)start_ticks) / PL_TICKS_PER_SEC;

	if (current_ticks > pts_ticks) {
		assert(frame->channels < AV_NUM_DATA_POINTERS);

		for (int i = 0; i < frame->nb_samples; ++i) {
			for (int c = 0; c < frame->channels; ++c) {
				float* data = (float*)frame->data[c];
				channels_buffer[(i*frame->channels) + c] = data[i];
			}
		}

		pl_audio_render_ex(
			channels_buffer,
			frame->nb_samples * frame->channels * 4
		);



		av_frame_unref(frame);
		frame = NULL;
	}

}

/*
void audio_callback(void* userdata, u8* stream, int len)
{
	if ( audio_len == 0 ) {
		return;
	}
	len = ( len > audio_len ? audio_len : len );
	SDL_MixAudioFormat(stream, audio_pos, AUDIO_F32SYS, len, SDL_MIX_MAXVOLUME);
	audio_pos += len;
	audio_len -= len;
}
*/


void codecs_study_main(int argc, char** argv)
{
	int err;
	struct pl_buffer file;
	
	AVIOContext *av_io = NULL;

	
	AVCodecParameters* audio_codecpar = NULL;

	
	AVCodecParameters* video_codecpar = NULL;
	
	AVCodec* audio_codec = NULL;
	

	AVCodec* video_codec = NULL;
	


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
		} else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			video_stream = stream;
			video_codecpar = video_stream->codecpar;
		}
	}

	assert(audio_stream != NULL && audio_codecpar != NULL);
	assert(video_stream != NULL && video_codecpar != NULL);

	audio_codec = avcodec_find_decoder(audio_codecpar->codec_id);
	assert(audio_codec != NULL);

	audio_codec_ctx = avcodec_alloc_context3(audio_codec);
	assert(audio_codec_ctx != NULL);

	err = avcodec_parameters_to_context(audio_codec_ctx, audio_codecpar);
	assert(err == 0);

	err = avcodec_open2(audio_codec_ctx, audio_codec, NULL);
	assert(err == 0);


	video_codec = avcodec_find_decoder(video_codecpar->codec_id);
	assert(video_codec != NULL);

	video_codec_ctx = avcodec_alloc_context3(video_codec);
	assert(video_codec_ctx != NULL);

	err = avcodec_parameters_to_context(video_codec_ctx, video_codecpar);
	assert(err == 0);

	err = avcodec_open2(video_codec_ctx, video_codec, NULL);
	assert(err == 0);
	

	pl_cfg_video(video_codec_ctx->width, video_codec_ctx->height, PL_VIDEO_FMT_YUV);
	pl_cfg_audio(audio_codec_ctx->sample_rate, audio_codec_ctx->channels, PL_AUDIO_FMT_F32SYS);

	channels_buffer = malloc(4 * audio_codec_ctx->channels * audio_codec_ctx->sample_rate);
	assert(channels_buffer != NULL);

	atime_base = av_q2d(audio_stream->time_base);
	vtime_base = av_q2d(video_stream->time_base);

	log_info("AUDIO FREQUENCY: %d", audio_codec_ctx->sample_rate);
	log_info("CHANNELS: %d", audio_codec_ctx->channels);
	log_info("SAMPLE FMT: %s", av_get_sample_fmt_name(audio_codec_ctx->sample_fmt));
	log_info("FRAME SIZE: %d", audio_codec_ctx->frame_size);
	log_info("initial padding: %d", audio_codec_ctx->initial_padding);
	log_info("trailing padding: %d", audio_codec_ctx->trailing_padding);
	log_info("atime_base: %.6lf", atime_base);

	log_info("width: %d", video_codec_ctx->width);
	log_info("height: %d", video_codec_ctx->height);
	log_info("vtime_base: %.6lf", vtime_base);

	av_frame = av_frame_alloc();
	assert(av_frame != NULL);

	av_packet = av_packet_alloc();
	assert(av_packet != NULL);

	frame_queue_init(&video_queue);
	frame_queue_init(&audio_queue);


	while (!pl_close_request()) {
		if (video_queue.cnt < FRAME_QUEUE_SIZE && audio_queue.cnt < FRAME_QUEUE_SIZE)
			decode_and_queue_frame();
		play_audio();
		play_video();
	}


	free(channels_buffer);
	
	av_frame_free(&av_frame);
	av_packet_free(&av_packet);
	
	avcodec_close(video_codec_ctx);
	avcodec_free_context(&video_codec_ctx);

	avcodec_close(audio_codec_ctx);
	avcodec_free_context(&audio_codec_ctx);
	
	avformat_close_input(&av_fmt_ctx);
	avformat_free_context(av_fmt_ctx);

	av_free(av_io->buffer);
	av_free(av_io);

	pl_free_buffer_ex(av_free, &file);
}


