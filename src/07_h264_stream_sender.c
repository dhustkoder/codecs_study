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

struct rgb24 {
	u8 r, g, b;
};
struct rgb24 framebuffer[HEIGHT][WIDTH];

pl_udp_socket_t send_sock;
pl_udp_socket_t recv_sock;


static void update_fb(void)
{
	for (int i = 0; i < HEIGHT; ++i) {
		for (int j = 0; j < WIDTH; ++j) {
			framebuffer[i][j] = (struct rgb24) {
				rand()&0xFF,
				rand()&0xFF,
				rand()&0xFF
			};
		}
	}
}


/*
 * Video encoding example
 */
AVCodec *codec;
AVCodecContext *c= NULL;
AVFrame *frame;
AVPacket pkt;

static void encoder_init(void)
{
	int ret;

	/* find the video encoder */
	codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	assert(codec != NULL);
	c = avcodec_alloc_context3(codec);
	assert(c != NULL);


	/* put sample parameters */
	c->bit_rate = 450000;
	/* resolution must be a multiple of two */
	c->width = WIDTH;
	c->height = HEIGHT;
	/* frames per second */
	c->time_base= (AVRational){1,60};
	c->gop_size = 24; /* emit one intra frame every ten frames */
	c->max_b_frames=1;
	c->pix_fmt = AV_PIX_FMT_YUV420P;
	av_opt_set(c->priv_data, "preset", "slow", 0);
	
	/* open it */
	if (avcodec_open2(c, codec, NULL) < 0)
		assert(false);

	frame = av_frame_alloc();
	assert(frame != NULL);

	frame->format = c->pix_fmt;
	frame->width  = c->width;
	frame->height = c->height;
	/* the image can be allocated by any means and av_image_alloc() is
	 * just the most convenient way if av_malloc() is to be used */
	ret = av_image_alloc(
		frame->data, 
		frame->linesize,
		c->width, c->height,
		c->pix_fmt, 32
	);
	assert(ret >= 0);

	av_init_packet(&pkt);
	pkt.data = NULL;    // packet data will be allocated by the encoder
	pkt.size = 0;
}

static void encoder_term(void)
{
	avcodec_close(c);
	av_free(c);
	av_freep(&frame->data[0]);
	av_frame_free(&frame);
}

u8* data_itr = NULL;
u64 size_cnt = 0;

static void encode_and_send_fb(void)
{
	static int pts = 0;
	int ret, x, y, got_output;
	

	/* prepare a dummy image */
	/* Y */
	for(y=0;y<c->height;y++) {
		for(x=0;x<c->width;x++) {
			frame->data[0][y * frame->linesize[0] + x] = x + y + rand();
		}
	}

	/* Cb and Cr */
	for(y=0;y<c->height/2;y++) {
		for(x=0;x<c->width/2;x++) {
			frame->data[1][y * frame->linesize[1] + x] = 128 + y + 2;
			frame->data[2][y * frame->linesize[2] + x] = 64 + x + 5;
		}
	}
	pl_video_render_yuv(
		frame->data[0], frame->data[1], frame->data[2],
		frame->linesize[0], frame->linesize[1], frame->linesize[2]
	);
	frame->pts = pts++;
	/* encode the image */
	ret = avcodec_encode_video2(c, &pkt, frame, &got_output);
	assert(ret >= 0);

	if (got_output) {
		data_itr = pkt.data;
		size_cnt = pkt.size;
	}


}


void codecs_study_main(int argc, char** argv)
{

	encoder_init();
	send_sock = pl_socket_udp_sender_create("127.0.0.1", 7171);
	recv_sock = pl_socket_udp_receiver_create(7172);

	srand(time(NULL));
	pl_cfg_video(WIDTH, HEIGHT, PL_VIDEO_FMT_YUV);

	while (!pl_close_request()) {
		update_fb();
		if (size_cnt > 0) {
			char send_str_buf[24] = "snd_ready\0";
			char recv_str_buf[24] = "\0";


			pl_socket_udp_recv(recv_sock, recv_str_buf, strlen("recv_ready"));
			log_info("got str: %s", recv_str_buf);
			if (strcmp(recv_str_buf, "recv_ready") != 0) {
				size_cnt = 0;
				continue;
			}
			pl_socket_udp_send(send_sock, send_str_buf, strlen(send_str_buf));

			pl_socket_udp_send(send_sock, &size_cnt, sizeof(size_cnt));

			u64 response;
			pl_socket_udp_recv(recv_sock, &response, sizeof(response));
			
			if (response != size_cnt) {
				size_cnt = 0;
				continue;
			}

			pl_socket_udp_send(send_sock, data_itr, size_cnt);
			size_cnt = 0;
			continue;
		} else {
			av_free_packet(&pkt);
			encode_and_send_fb();
		}

	}


	encoder_term();
}


