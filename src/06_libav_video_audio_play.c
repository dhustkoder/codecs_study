#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include "platform_layer.h"

#define PKT_QUEUE_SIZE (8)

struct pkt_queue {
	AVPacket* pkts[PKT_QUEUE_SIZE];
	AVPacket** in_itr;
	AVPacket** out_itr;
	int cnt;
};

static struct pkt_queue video_queue;
static struct pkt_queue audio_queue;


static void pkt_queue_init(struct pkt_queue* fq)
{
	memset(fq->pkts, 0, PKT_QUEUE_SIZE * sizeof(*fq->pkts));
	fq->in_itr = fq->pkts;
	fq->out_itr = fq->pkts;
	fq->cnt = 0;
}


static void pkt_queue_add(struct pkt_queue* fq, AVPacket* src)
{
	assert(fq->cnt < PKT_QUEUE_SIZE);


	*fq->in_itr = av_packet_clone(src);
	assert(*fq->in_itr != NULL);
	++fq->in_itr;
	
	if ((fq->in_itr - fq->pkts) == PKT_QUEUE_SIZE)
		fq->in_itr = fq->pkts;
	
	++fq->cnt;
} 


static void pkt_queue_rem(struct pkt_queue* fq, AVPacket** dest)
{
	assert(*fq->out_itr != NULL);
	*dest = *fq->out_itr;
	*fq->out_itr = NULL;
	++fq->out_itr;
	
	if ((fq->out_itr - fq->pkts) == PKT_QUEUE_SIZE)
		fq->out_itr = fq->pkts;

	--fq->cnt;
}


static AVFormatContext *av_fmt_ctx = NULL;
static AVStream* audio_stream = NULL;
static AVStream* video_stream = NULL;
static AVCodecContext* audio_codec_ctx = NULL;
static AVCodecContext* video_codec_ctx = NULL;

static AVFrame* av_frame = NULL;
static AVPacket* av_packet = NULL;

static u8* channels_buffer = NULL;
static u8* channels_buffer_end = NULL;
static u8* channels_buffer_wp = NULL;
static const u8* channels_buffer_rp = NULL;

static double atime_base;
static double vtime_base;
static double playing_audio_pts = 0;
static double audio_delay = 0;
static tick_t start_ticks = 0;


static bool read_and_queue_pkt(void)
{
	if (av_read_frame(av_fmt_ctx, av_packet) >= 0) {
		// AVCodecContext* cctx;
		assert(av_packet != NULL);
		if (av_packet->stream_index == audio_stream->index) {
			log_info("adding an audio_pkt");
			pkt_queue_add(&audio_queue, av_packet);
		} else if (av_packet->stream_index == video_stream->index) {
			log_info("adding a video_pkt");
			pkt_queue_add(&video_queue, av_packet);
			log_info("added a video_pkt");
		} else {
			goto Lunref_packet;
		}

		/*
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
		*/

		Lunref_packet:
		av_packet_unref(av_packet);
		return true;
	} else {
		return false;
	}
}

static void play_video(void)
{
	static AVPacket* pkt = NULL;
	static AVFrame* frame = NULL;

	if (pkt == NULL) {
		if (video_queue.cnt > 0)
			pkt_queue_rem(&video_queue, &pkt);
		else
			return;
	}

	if (frame == NULL) {
		int err = avcodec_send_packet(video_codec_ctx, pkt);
		assert(err == 0);
		
		err = avcodec_receive_frame(video_codec_ctx, frame);
		if (err == AVERROR(EAGAIN)) {
			assert(frame == NULL);
			return;
		}
	}

	const double pts_ticks = frame->pts  * vtime_base;
	const double current_ticks = ((double)pl_get_ticks() - (double)start_ticks) / PL_TICKS_PER_SEC;

	if ((playing_audio_pts - audio_delay) >= pts_ticks) {
		log_info("video pts ticks: %.2lf", pts_ticks);
		pl_video_render_yuv(
			frame->data[0], frame->data[1], frame->data[2],
			frame->linesize[0], frame->linesize[1], frame->linesize[2]
		);

		av_frame_unref(frame);
		av_packet_unref(pkt);
		frame = NULL;
		pkt = NULL;
	}
}

static void play_audio(void)
{
	static AVPacket* pkt = NULL;
	static AVFrame* frame = NULL;

	if (pkt == NULL) {
		if (audio_queue.cnt > 0)
			pkt_queue_rem(&audio_queue, &pkt);
		else
			return;
	}

	if (frame == NULL) {
		int err = avcodec_send_packet(audio_codec_ctx, pkt);
		assert(err == 0);
		
		err = avcodec_receive_frame(audio_codec_ctx, frame);
		if (err == AVERROR(EAGAIN))
			return;
	}

	const double pts_ticks = frame->pts * atime_base;
	const double current_ticks = ((double)pl_get_ticks() - (double)start_ticks) / PL_TICKS_PER_SEC;

	if (current_ticks >= pts_ticks) {
		assert(frame->channels < AV_NUM_DATA_POINTERS);

		playing_audio_pts = pts_ticks;
		log_info("PLAYING AUDIO PTS: %.2lf", playing_audio_pts);

		for (int i = 0; i < frame->nb_samples; ++i) {
			for (int c = 0; c < frame->channels; ++c) {
				float* data = frame->data[c];
				*((float*)channels_buffer_wp) = data[i];
				channels_buffer_wp += 4;
				if (channels_buffer_wp >= channels_buffer_end)
					channels_buffer_wp = channels_buffer;
			}
		}

		av_frame_unref(frame);
		av_packet_unref(pkt);
		frame = NULL;
		pkt = NULL;
	}

}

void audio_callback(void* userdata, u8* stream, int stream_len)
{
	int audio_len;
	int len;
	bool double_send = false;

	audio_delay = (stream_len / 4.f) / audio_codec_ctx->sample_rate;

Lsend:
	if (channels_buffer_rp == channels_buffer_wp) {
		memset(stream, 0, stream_len);
		return;
	}

	if (channels_buffer_rp < channels_buffer_wp) {
		audio_len = channels_buffer_wp - channels_buffer_rp;
	} else {
		audio_len = channels_buffer_end - channels_buffer_rp;
		double_send = true;
	}

	len = ( stream_len > audio_len ? audio_len : stream_len );
	memcpy(stream, channels_buffer_rp, len);

	channels_buffer_rp += len;
	if (channels_buffer_rp >= channels_buffer_end)
		channels_buffer_rp = channels_buffer;

	if (double_send) {
		double_send = false;
		stream_len -= len;
		stream += len;
		goto Lsend;
	}

}


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
	pl_cfg_audio_ex(audio_codec_ctx->sample_rate, audio_codec_ctx->channels, PL_AUDIO_FMT_F32SYS, audio_callback);

	channels_buffer = malloc(8 * 4 * audio_codec_ctx->channels * audio_codec_ctx->sample_rate);
	channels_buffer_end = channels_buffer + (8 * 4 * audio_codec_ctx->channels * audio_codec_ctx->sample_rate);
	channels_buffer_rp = channels_buffer;
	channels_buffer_wp = channels_buffer;
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

	pkt_queue_init(&video_queue);
	pkt_queue_init(&audio_queue);

	while (video_queue.cnt < PKT_QUEUE_SIZE && audio_queue.cnt < PKT_QUEUE_SIZE) {
		if (!read_and_queue_pkt())
			break;
	}

	log_info("video_queue beg size: %d", video_queue.cnt);
	log_info("audio_queue beg size: %d", audio_queue.cnt);

	start_ticks = pl_get_ticks();

	while (!pl_close_request()) {
		play_audio();
		play_video();
		if (video_queue.cnt < PKT_QUEUE_SIZE && audio_queue.cnt < PKT_QUEUE_SIZE)
			read_and_queue_pkt();
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


