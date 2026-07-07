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
    char request[1024];
    const char *header =
        "HTTP/1.0 200 OK\r\n"
        "Server: ov5640_capture\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache\r\n"
        "Pragma: no-cache\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
        "\r\n";

    printf("Waiting for HTTP client...\n");
    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd == -1) {
        perror("accept");
        return -1;
    }

    (void)recv(client_fd, request, sizeof(request), 0);

    if (send_all(client_fd, header, strlen(header)) != 0) {
        perror("send");
        close(client_fd);
        return -1;
    }

    printf("HTTP client connected\n");
    return client_fd;
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

void http_mjpeg_close(int fd)
{
    if (fd != -1) {
        close(fd);
    }
}
