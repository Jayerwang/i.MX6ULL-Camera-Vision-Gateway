#ifndef STREAM_HTTP_MJPEG_H
#define STREAM_HTTP_MJPEG_H

#include <stddef.h>

int http_mjpeg_listen(int port);
int http_mjpeg_accept_client(int server_fd);
int http_mjpeg_accept_request(int server_fd, char *path, size_t path_size);
int http_mjpeg_send_stream_header(int client_fd);
int http_mjpeg_send_frame(int client_fd, const void *data, size_t size);
int http_mjpeg_send_jpeg_response(int client_fd, const void *data, size_t size);
int http_mjpeg_send_text_response(int client_fd, const char *status, const char *body);
int http_mjpeg_send_not_found(int client_fd);
void http_mjpeg_close(int fd);

#endif
