#include "platform_layer.h"


u8 fb[PL_SCR_W * PL_SCR_H * 3];




void dvs_main(void)
{
	memset(fb, 0xFF, sizeof fb);
	while (!pl_close_request()) {
		pl_render_buffer(fb);
	}
}


