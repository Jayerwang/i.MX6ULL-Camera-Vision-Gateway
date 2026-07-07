#ifndef STREAM_HTTP_MJPEG_H
#define STREAM_HTTP_MJPEG_H

#include <stddef.h>

int http_mjpeg_listen(int port);
int http_mjpeg_accept_client(int server_fd);
int http_mjpeg_send_frame(int client_fd, const void *data, size_t size);
void http_mjpeg_close(int fd);

#endif
