#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include "platform_layer.h"

#define PKT_QUEUE_SIZE (1024 * 64)

struct pkt_queue {
	AVPacket* pkts[PKT_QUEUE_SIZE];
	AVPacket** in_itr;
	AVPacket** out_itr;
	int cnt;
};

static struct pkt_queue video_queue;
static struct pkt_queue audio_queue;


static void pkt_queue_init(struct pkt_queue* pq)
{
	memset(pq->pkts, 0, PKT_QUEUE_SIZE * sizeof(*pq->pkts));
	pq->in_itr = pq->pkts;
	pq->out_itr = pq->pkts;
	pq->cnt = 0;
}


static void pkt_queue_add(struct pkt_queue* pq, AVPacket* src)
{
	assert(pq->cnt < PKT_QUEUE_SIZE);


	*pq->in_itr = av_packet_clone(src);
	assert(*pq->in_itr != NULL);
	++pq->in_itr;
	
	if ((pq->in_itr - pq->pkts) == PKT_QUEUE_SIZE)
		pq->in_itr = pq->pkts;
	
	++pq->cnt;
} 


static void pkt_queue_rem(struct pkt_queue* pq, AVPacket** dest)
{
	assert(*pq->out_itr != NULL);
	*dest = *pq->out_itr;
	*pq->out_itr = NULL;
	++pq->out_itr;
	
	if ((pq->out_itr - pq->pkts) == PKT_QUEUE_SIZE)
		pq->out_itr = pq->pkts;

	--pq->cnt;
}

static void pkt_queue_clean(struct pkt_queue* pq)
{
	AVPacket* pkt;
	while (pq->cnt > 0) {
		pkt_queue_rem(pq, &pkt);
		av_packet_unref(pkt);
		av_packet_free(&pkt);
	}
}

static AVFormatContext *av_fmt_ctx = NULL;
static AVStream* audio_stream = NULL;
static AVStream* video_stream = NULL;
static AVCodecContext* audio_codec_ctx = NULL;
static AVCodecContext* video_codec_ctx = NULL;

static AVFrame* video_frame = NULL;
static AVFrame* audio_frame = NULL;
static AVPacket* av_packet = NULL;

static u8* channels_buffer = NULL;

static double atime_base;
static double vtime_base;


static bool read_and_queue_pkt(void)
{
	if (av_read_frame(av_fmt_ctx, av_packet) == 0) {

		if (av_packet->stream_index == audio_stream->index) {
			pkt_queue_add(&audio_queue, av_packet);
		} else if (av_packet->stream_index == video_stream->index) {
			pkt_queue_add(&video_queue, av_packet);
		}

		av_packet_unref(av_packet);
		return true;
	} else {
		return false;
	}
}


static void decode_and_play_frame(
	struct pkt_queue* pq,
	AVFrame* frame,
	bool* frame_ready,
	AVCodecContext* cc,
	tick_t* start_ticks,
	double time_base,
	void(*render_frame)(AVFrame*)
)
{
	AVPacket* pkt;

	while (!(*frame_ready)) {

		if (pq->cnt == 0)
			return;

		pkt_queue_rem(pq, &pkt);

		int err = avcodec_send_packet(cc, pkt);
		assert(err == 0);
		err = avcodec_receive_frame(cc, frame);
		av_packet_unref(pkt);
		av_packet_free(&pkt);
		
		assert(err == AVERROR(EAGAIN) || err == 0);

		if (err == 0)
			*frame_ready = true;

	}

	if (*start_ticks == 0)
		*start_ticks = pl_get_ticks();

	const double pts_ticks = frame->pts  * time_base;
	const double current_ticks = ((double)pl_get_ticks() - (double)*start_ticks) / PL_TICKS_PER_SEC;

	if (current_ticks > pts_ticks) {
		render_frame(frame);
		av_frame_unref(frame);
		*frame_ready = false;
	}

}

static void render_video_frame(AVFrame* frame)
{
	pl_video_render_yuv(
		frame->data[0], frame->data[1], frame->data[2],
		frame->linesize[0], frame->linesize[1], frame->linesize[2]
	);
}

static void render_audio_frame(AVFrame* frame)
{
	assert(frame->channels < AV_NUM_DATA_POINTERS);

	float* channels_buffer_wp =  (float*)channels_buffer;
	for (int i = 0; i < frame->nb_samples; ++i) {
		for (int c = 0; c < frame->channels; ++c) {
			float* data = (float*) frame->data[c];
			*channels_buffer_wp = data[i];
			++channels_buffer_wp;
		}
	}

	pl_audio_render_ex(
		channels_buffer,
		sizeof(float) * frame->nb_samples * frame->channels
	);
}

static void play_video(void)
{
	static bool frame_ready = false;
	static tick_t start_ticks = 0;

	decode_and_play_frame(
		&video_queue,
		video_frame,
		&frame_ready,
		video_codec_ctx,
		&start_ticks,
		vtime_base,
		render_video_frame
	);
}

static void play_audio(void)
{
	static bool frame_ready = false;
	static tick_t start_ticks = 0;

	decode_and_play_frame(
		&audio_queue,
		audio_frame,
		&frame_ready,
		audio_codec_ctx,
		&start_ticks,
		atime_base,
		render_audio_frame
	);
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
	pl_cfg_audio(audio_codec_ctx->sample_rate, audio_codec_ctx->channels, PL_AUDIO_FMT_F32SYS);

	const size_t cbsize = 4 * audio_codec_ctx->channels * audio_codec_ctx->sample_rate;
	channels_buffer = malloc(cbsize);
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

	video_frame = av_frame_alloc();
	assert(video_frame != NULL);

	audio_frame = av_frame_alloc();
	assert(audio_frame != NULL);

	av_packet = av_packet_alloc();
	assert(av_packet != NULL);

	pkt_queue_init(&video_queue);
	pkt_queue_init(&audio_queue);


	while (video_queue.cnt < PKT_QUEUE_SIZE && audio_queue.cnt < PKT_QUEUE_SIZE)
		if (!read_and_queue_pkt())
			break;

	
	while (!pl_close_request()) {
		play_audio();
		play_video();
//		log_info("video_queue.cnt %d", video_queue.cnt);
//		log_info("audio_queue.cnt %d", audio_queue.cnt);
		if (video_queue.cnt < PKT_QUEUE_SIZE && audio_queue.cnt < PKT_QUEUE_SIZE)
			read_and_queue_pkt();
	}

	pkt_queue_clean(&video_queue);
	pkt_queue_clean(&audio_queue);

	free(channels_buffer);
	
	av_frame_free(&video_frame);
	av_frame_free(&audio_frame);
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


