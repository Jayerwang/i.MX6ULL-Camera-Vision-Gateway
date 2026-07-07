#include "camera_capture.h"
#include "camera_device.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_usage(const char *program)
{
    printf("Usage: %s [options]\n", program);
    printf("Options:\n");
    printf("  -d DEV   V4L2 device node, default /dev/video1\n");
    printf("  -w W     Width, default 1024\n");
    printf("  -h H     Height, default 600\n");
    printf("  -f FMT   Pixel format: YUYV | NV12 | RGBP | MJPG | MJPEG, default YUYV\n");
    printf("  -r FPS   Frame rate, default 30\n");
    printf("  -n N     Frame count, default 10, 0 means continuous capture\n");
    printf("  -o FILE  Output file, default output.yuv\n");
    printf("      --no-save       Capture and measure frames without writing output file\n");
    printf("      --save-frames DIR  Save each captured frame into DIR\n");
    printf("      --http-mjpeg PORT  Stream MJPEG over HTTP on PORT\n");
    printf("  -L, --list-formats  List supported formats, frame sizes and fps\n");
    printf("      --help          Show this help message\n");
}

static int parse_port(const char *value)
{
    const char *port_text = strrchr(value, ':');
    int port;

    if (port_text != NULL) {
        ++port_text;
    } else {
        port_text = value;
    }

    port = atoi(port_text);
    if (port <= 0 || port > 65535) {
        return -1;
    }

    return port;
}

static int parse_args(int argc, char **argv, camera_config_t *config, int *list_formats)
{
    int opt;
    static const struct option long_options[] = {
        {"list-formats", no_argument, NULL, 'L'},
        {"no-save", no_argument, NULL, 1001},
        {"save-frames", required_argument, NULL, 1002},
        {"http-mjpeg", required_argument, NULL, 1003},
        {"help", no_argument, NULL, 1000},
        {NULL, 0, NULL, 0}
    };

    while ((opt = getopt_long(argc, argv, "d:w:h:f:r:n:o:L", long_options, NULL)) != -1) {
        switch (opt) {
        case 'd':
            strncpy(config->device, optarg, sizeof(config->device) - 1);
            config->device[sizeof(config->device) - 1] = '\0';
            break;
        case 'w':
            config->width = atoi(optarg);
            break;
        case 'h':
            config->height = atoi(optarg);
            break;
        case 'f':
            if (camera_parse_pixel_format(optarg, &config->pixel_format) != 0) {
                fprintf(stderr, "Unsupported pixel format: %s\n", optarg);
                return -1;
            }
            break;
        case 'r':
            config->fps = atoi(optarg);
            break;
        case 'n':
            config->frame_count = atoi(optarg);
            break;
        case 'o':
            strncpy(config->output, optarg, sizeof(config->output) - 1);
            config->output[sizeof(config->output) - 1] = '\0';
            break;
        case 'L':
            *list_formats = 1;
            break;
        case 1001:
            config->no_save = 1;
            break;
        case 1002:
            config->save_frames = 1;
            strncpy(config->frame_dir, optarg, sizeof(config->frame_dir) - 1);
            config->frame_dir[sizeof(config->frame_dir) - 1] = '\0';
            break;
        case 1003:
            config->http_port = parse_port(optarg);
            if (config->http_port < 0) {
                fprintf(stderr, "Invalid HTTP MJPEG port: %s\n", optarg);
                return -1;
            }
            config->http_mjpeg = 1;
            break;
        case 1000:
            print_usage(argv[0]);
            return 1;
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    if (config->width <= 0 || config->height <= 0 || config->fps <= 0 || config->frame_count < 0) {
        fprintf(stderr, "Invalid capture arguments\n");
        return -1;
    }
    if (config->no_save && config->save_frames) {
        fprintf(stderr, "--no-save and --save-frames cannot be used together\n");
        return -1;
    }
    if (config->http_mjpeg && (config->no_save || config->save_frames)) {
        fprintf(stderr, "--http-mjpeg cannot be used with --no-save or --save-frames in this stage\n");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    camera_config_t config;
    camera_context_t ctx;
    int ret = EXIT_FAILURE;
    int parse_ret;
    int list_formats = 0;

    camera_config_init(&config);
    parse_ret = parse_args(argc, argv, &config, &list_formats);
    if (parse_ret < 0) {
        return EXIT_FAILURE;
    }
    if (parse_ret > 0) {
        return EXIT_SUCCESS;
    }

    if (list_formats) {
        return camera_list_formats(config.device) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    printf("OV5640 V4L2 capture\n");
    printf("Device: %s\n", config.device);
    printf("Format: %dx%d %s @ %d fps\n",
           config.width,
           config.height,
           camera_pixel_format_name(config.pixel_format),
           config.fps);
    printf("Frames: %s\n", config.frame_count == 0 ? "continuous" : "limited");
    if (config.no_save) {
        printf("Output: (disabled)\n");
    } else if (config.save_frames) {
        printf("Output frames: %s\n", config.frame_dir);
    } else if (config.http_mjpeg) {
        printf("HTTP MJPEG: 0.0.0.0:%d\n", config.http_port);
    } else {
        printf("Output: %s\n", config.output);
    }

    if (camera_open(&ctx, &config) != 0) {
        goto out;
    }
    if (camera_configure(&ctx) != 0) {
        goto out_close;
    }
    if (camera_start(&ctx) != 0) {
        goto out_close;
    }
    if (camera_capture_to_file(&ctx) != 0) {
        goto out_stop;
    }

    ret = EXIT_SUCCESS;

out_stop:
    camera_stop(&ctx);
out_close:
    camera_close(&ctx);
out:
    return ret;
}
