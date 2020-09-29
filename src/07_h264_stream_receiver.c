#include <time.h>

#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libavutil/pixdesc.h>

#include "platform_layer.h"

#define WIDTH  (1280)
#define HEIGHT (720)
#define INBUF_SIZE (1024 * 8)


pl_udp_socket_t send_sock;
pl_udp_socket_t recv_sock;


/*
 * Video decoding example
 */
AVCodec *codec;
AVCodecContext *c= NULL;
int frame_count;
AVFrame *frame;
u8* inbuf = NULL;
size_t inbuf_size = 0;
AVPacket avpkt;


static void decoder_init(void)
{
	inbuf = av_malloc(INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
	inbuf_size = INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE;
	av_init_packet(&avpkt);
	avpkt.data = inbuf;
	avpkt.size = 0;
	/* set end of buffer to 0 (this ensures that no overreading happens for damaged mpeg streams) */
	memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);
	/* find the mpeg1 video decoder */
	codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	c = avcodec_alloc_context3(codec);
	assert(c != NULL);
	if(codec->capabilities&AV_CODEC_CAP_TRUNCATED)
		c->flags|= AV_CODEC_FLAG_TRUNCATED; /* we do not send complete frames */
	/* For some codecs, such as msmpeg4 and mpeg4, width and height
	   MUST be initialized there because this information is not
	   available in the bitstream. */
	/* open it */

	if (avcodec_open2(c, codec, NULL) < 0) {
		assert(false);
	}


	frame = av_frame_alloc();
	assert(frame != NULL);



	frame_count = 0;
}

static void decoder_term(void)
{
	avcodec_close(c);
	av_free(c);
	av_frame_free(&frame);
}

static int decode_write_frame(
	AVCodecContext *avctx,
	AVFrame *frame,
	AVPacket *pkt,
	int last
)
{
	int len, got_frame;
	len = avcodec_decode_video2(avctx, frame, &got_frame, pkt);
	if (len < 0)
		return -1;

	if (got_frame) {
		pl_video_render_yuv(
			frame->data[0], frame->data[1], frame->data[2],
			frame->linesize[0], frame->linesize[1], frame->linesize[2]
		);
		frame_count++;
	}

	if (pkt->data) {
		pkt->size -= len;
		pkt->data += len;
	}

	return 0;

}

static void recv_and_decode_and_draw(void)
{
	u64 incoming_size;
	if (pl_socket_udp_recv(recv_sock, &incoming_size, sizeof(incoming_size)) != sizeof(incoming_size))
		return;

	if (incoming_size > inbuf_size) {
		inbuf = av_realloc(inbuf, incoming_size + AV_INPUT_BUFFER_PADDING_SIZE);
		inbuf_size = incoming_size + AV_INPUT_BUFFER_PADDING_SIZE;
	}

	pl_socket_udp_send(send_sock, &incoming_size, sizeof(incoming_size));

	u64 recv_size = pl_socket_udp_recv(recv_sock, inbuf, incoming_size);

	if (recv_size != incoming_size)
		return;

	avpkt.size = recv_size;
	avpkt.data = inbuf;

	while (avpkt.size > 0) {
		if (decode_write_frame(c, frame, &avpkt, 0) < 0) {
			printf("failed with pkt.size: %d\n", avpkt.size);
			return;
		}
	}

}




void codecs_study_main(int argc, char** argv)
{
	decoder_init();
	send_sock = pl_socket_udp_sender_create("127.0.0.1", 7172);
	recv_sock = pl_socket_udp_receiver_create(7171);

	pl_cfg_video(WIDTH, HEIGHT, PL_VIDEO_FMT_YUV);

	tick_t ticks = pl_get_ticks(); 

	while (!pl_close_request()) {
		recv_and_decode_and_draw();
		if ((pl_get_ticks() - ticks) > 1000) {
			log_info("FPS: %d", frame_count);
			frame_count = 0;
			ticks = pl_get_ticks();
		}
	}


	decoder_term();
}


