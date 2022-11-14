#ifndef W32HTTP_SERVER_H
#define W32HTTP_SERVER_H
#include "common.h"

struct server_config_t {
	char *listen_addr;
	u16   listen_port;

#ifdef WIN32
	HWND	main_window;
	HANDLE	lock;
#endif

};

int start_server(struct server_config_t *cfg);

#endif //W32HTTP_SERVER_H