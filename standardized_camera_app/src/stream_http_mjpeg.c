#include "stream_http_mjpeg.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int send_all(int fd, const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t sent = 0;

    while (sent < size) {
        ssize_t ret = send(fd, p + sent, size - sent, 0);
        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (ret == 0) {
            return -1;
        }
        sent += (size_t)ret;
    }

    return 0;
}

int http_mjpeg_listen(int port)
{
    int fd;
    int opt = 1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return -1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 1) == -1) {
        perror("listen");
        close(fd);
        return -1;
    }

    printf("HTTP MJPEG server listening on 0.0.0.0:%d\n", port);
    printf("Open http://BOARD_IP:%d/stream in a browser\n", port);
    return fd;
}

int http_mjpeg_accept_client(int server_fd)
{
    int client_fd;
    char path[256];

    client_fd = http_mjpeg_accept_request(server_fd, path, sizeof(path));
    if (client_fd == -1) {
        return -1;
    }

    if (http_mjpeg_send_stream_header(client_fd) != 0) {
        close(client_fd);
        return -1;
    }

    return client_fd;
}

int http_mjpeg_accept_request(int server_fd, char *path, size_t path_size)
{
    int client_fd;
    char request[1024];
    ssize_t received;
    char parsed_path[256];

    if (path == NULL || path_size == 0) {
        return -1;
    }
    path[0] = '\0';

    printf("Waiting for HTTP client...\n");
    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd == -1) {
        perror("accept");
        return -1;
    }

    received = recv(client_fd, request, sizeof(request) - 1, 0);
    if (received <= 0) {
        perror("recv");
        close(client_fd);
        return -1;
    }
    request[received] = '\0';

    parsed_path[0] = '\0';
    if (sscanf(request, "GET %255s", parsed_path) != 1) {
        strncpy(parsed_path, "/", sizeof(parsed_path) - 1);
        parsed_path[sizeof(parsed_path) - 1] = '\0';
    }

    strncpy(path, parsed_path, path_size - 1);
    path[path_size - 1] = '\0';
    printf("HTTP client connected, path=%s\n", path);
    return client_fd;
}

int http_mjpeg_send_stream_header(int client_fd)
{
    const char *header =
        "HTTP/1.0 200 OK\r\n"
        "Server: ov5640_capture\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache\r\n"
        "Pragma: no-cache\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
        "\r\n";

    if (send_all(client_fd, header, strlen(header)) != 0) {
        perror("send");
        return -1;
    }

    return 0;
}

int http_mjpeg_send_frame(int client_fd, const void *data, size_t size)
{
    char part_header[128];
    int len;

    len = snprintf(part_header, sizeof(part_header),
                   "--frame\r\n"
                   "Content-Type: image/jpeg\r\n"
                   "Content-Length: %lu\r\n"
                   "\r\n",
                   (unsigned long)size);
    if (len < 0 || (size_t)len >= sizeof(part_header)) {
        return -1;
    }

    if (send_all(client_fd, part_header, (size_t)len) != 0) {
        return -1;
    }
    if (send_all(client_fd, data, size) != 0) {
        return -1;
    }
    if (send_all(client_fd, "\r\n", 2) != 0) {
        return -1;
    }

    return 0;
}

int http_mjpeg_send_jpeg_response(int client_fd, const void *data, size_t size)
{
    char header[256];
    int len;

    len = snprintf(header, sizeof(header),
                   "HTTP/1.0 200 OK\r\n"
                   "Server: ov5640_capture\r\n"
                   "Connection: close\r\n"
                   "Cache-Control: no-cache\r\n"
                   "Content-Type: image/jpeg\r\n"
                   "Content-Length: %lu\r\n"
                   "\r\n",
                   (unsigned long)size);
    if (len < 0 || (size_t)len >= sizeof(header)) {
        return -1;
    }

    if (send_all(client_fd, header, (size_t)len) != 0) {
        return -1;
    }
    if (send_all(client_fd, data, size) != 0) {
        return -1;
    }

    return 0;
}

int http_mjpeg_send_text_response(int client_fd, const char *status, const char *body)
{
    char header[256];
    int len;
    size_t body_size;

    if (status == NULL || body == NULL) {
        return -1;
    }

    body_size = strlen(body);
    len = snprintf(header, sizeof(header),
                   "HTTP/1.0 %s\r\n"
                   "Server: ov5640_capture\r\n"
                   "Connection: close\r\n"
                   "Cache-Control: no-cache\r\n"
                   "Content-Type: text/plain\r\n"
                   "Content-Length: %lu\r\n"
                   "\r\n",
                   status,
                   (unsigned long)body_size);
    if (len < 0 || (size_t)len >= sizeof(header)) {
        return -1;
    }

    if (send_all(client_fd, header, (size_t)len) != 0) {
        return -1;
    }
    if (send_all(client_fd, body, body_size) != 0) {
        return -1;
    }

    return 0;
}

int http_mjpeg_send_not_found(int client_fd)
{
    return http_mjpeg_send_text_response(client_fd,
                                         "404 Not Found",
                                         "not found\n");
}

void http_mjpeg_close(int fd)
{
    if (fd != -1) {
        close(fd);
    }
}
