#include "camera_device.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static int xioctl(int fd, unsigned long request, void *arg)
{
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);

    return ret;
}

static void fourcc_to_string(unsigned int fourcc, char out[5])
{
    out[0] = (char)(fourcc & 0xff);
    out[1] = (char)((fourcc >> 8) & 0xff);
    out[2] = (char)((fourcc >> 16) & 0xff);
    out[3] = (char)((fourcc >> 24) & 0xff);
    out[4] = '\0';
}

static void print_interval(const struct v4l2_frmivalenum *interval)
{
    if (interval->type == V4L2_FRMIVAL_TYPE_DISCRETE) {
        if (interval->discrete.numerator != 0) {
            double fps = (double)interval->discrete.denominator /
                         (double)interval->discrete.numerator;
            printf("      %.2f fps (%u/%u sec)\n",
                   fps,
                   interval->discrete.numerator,
                   interval->discrete.denominator);
        }
        return;
    }

    if (interval->type == V4L2_FRMIVAL_TYPE_STEPWISE ||
        interval->type == V4L2_FRMIVAL_TYPE_CONTINUOUS) {
        printf("      interval: %u/%u sec to %u/%u sec, step %u/%u sec\n",
               interval->stepwise.min.numerator,
               interval->stepwise.min.denominator,
               interval->stepwise.max.numerator,
               interval->stepwise.max.denominator,
               interval->stepwise.step.numerator,
               interval->stepwise.step.denominator);
    }
}

static void list_frame_intervals(int fd, unsigned int pixel_format, unsigned int width, unsigned int height)
{
    struct v4l2_frmivalenum interval;

    memset(&interval, 0, sizeof(interval));
    interval.pixel_format = pixel_format;
    interval.width = width;
    interval.height = height;

    while (xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == 0) {
        print_interval(&interval);

        if (interval.type != V4L2_FRMIVAL_TYPE_DISCRETE) {
            break;
        }
        ++interval.index;
    }
}

static void list_frame_sizes(int fd, unsigned int pixel_format)
{
    struct v4l2_frmsizeenum size;

    memset(&size, 0, sizeof(size));
    size.pixel_format = pixel_format;

    while (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0) {
        if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            printf("    %ux%u\n", size.discrete.width, size.discrete.height);
            list_frame_intervals(fd,
                                 pixel_format,
                                 size.discrete.width,
                                 size.discrete.height);
        } else if (size.type == V4L2_FRMSIZE_TYPE_STEPWISE ||
                   size.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
            printf("    %ux%u to %ux%u, step %ux%u\n",
                   size.stepwise.min_width,
                   size.stepwise.min_height,
                   size.stepwise.max_width,
                   size.stepwise.max_height,
                   size.stepwise.step_width,
                   size.stepwise.step_height);
            break;
        }

        ++size.index;
    }
}

int camera_list_formats(const char *device)
{
    struct stat st;
    struct v4l2_capability cap;
    struct v4l2_fmtdesc fmt;
    int fd;

    if (stat(device, &st) == -1) {
        fprintf(stderr, "Device not found: %s: %s\n", device, strerror(errno));
        return -1;
    }
    if (!S_ISCHR(st.st_mode)) {
        fprintf(stderr, "Not a character device: %s\n", device);
        return -1;
    }

    fd = open(device, O_RDWR | O_NONBLOCK, 0);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s: %s\n", device, strerror(errno));
        return -1;
    }

    memset(&cap, 0, sizeof(cap));
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        perror("VIDIOC_QUERYCAP");
        close(fd);
        return -1;
    }

    printf("Device: %s\n", device);
    printf("Driver: %s\n", cap.driver);
    printf("Card:   %s\n", cap.card);
    printf("Bus:    %s\n", cap.bus_info);
    printf("Caps:   0x%08x\n", cap.capabilities);

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "Device does not support video capture\n");
        close(fd);
        return -1;
    }

    printf("\nSupported formats:\n");

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    while (xioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0) {
        char fourcc[5];

        fourcc_to_string(fmt.pixelformat, fourcc);
        printf("  [%u] %s - %s\n", fmt.index, fourcc, fmt.description);
        list_frame_sizes(fd, fmt.pixelformat);

        ++fmt.index;
    }

    if (fmt.index == 0) {
        fprintf(stderr, "No capture formats found\n");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}
