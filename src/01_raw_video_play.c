#include "platform_layer.h"


#define FRAME_SIZE (PL_SCR_W * PL_SCR_H * 3)


void dvs_main(int argc, char** argv)
{
	assert(argc > 1);
	struct pl_buffer f;
	pl_read_file(argv[1], &f);

	log_info("file size: %llu", f.size);

	u8* itr = f.data;
	assert(f.size >= FRAME_SIZE);

	while (!pl_close_request()) {
		const u64 size_read = itr - f.data;

		if (size_read < f.size && (f.size - size_read) >= FRAME_SIZE) {
			itr += FRAME_SIZE;
		} else {
			itr = f.data;
		}

		pl_render_buffer(itr);
	}

	pl_free_buffer(&f);
}


