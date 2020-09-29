#include "platform_layer.h"



pl_udp_socket_t send_sock;
pl_udp_socket_t req_sock;


void codecs_study_main(int argc, char** argv)
{
	int request_size;
	void* bufp;

	struct pl_buffer file;

	pl_read_file(argv[1], &file);
	bufp = file.data;

	send_sock = pl_socket_udp_sender_create("127.0.0.1", 7171);
	req_sock = pl_socket_udp_receiver_create(7172);


	while (!pl_close_request()) {
		pl_socket_udp_recv(req_sock, &request_size, sizeof request_size);
		pl_socket_udp_send(send_sock, bufp, request_size);
		bufp += request_size;
		if ((void*)bufp >= (void*)(file.data + file.size))
			break;
	}

	pl_free_buffer(&file);
}
