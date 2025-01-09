#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

#include <wayland-client-core.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include "hyprland-toplevel-export-v1-client-protocol.h"

#define MAX_FILENAME_LENGTH 1024

struct wl_display *display;
struct wl_registry *registry;
struct hyprland_toplevel_export_manager_v1 *export_manager;
struct hyprland_toplevel_export_frame_v1 *export_frame;

struct wl_shm *shm;
struct wl_buffer *buffer;
uint32_t buffer_width = 0;
uint32_t buffer_height, buffer_stride, buffer_format;
void *shm_data;

static int frame_done = 0;
static int capture_failed = 0;

static void randname(char *buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A' + (r&15) + (r&16)*2;
        r >>= 5;
    }
}

static int create_shm_file() {
    int retries = 100;
    do {
        char name[] = "/hypr_window_picker_shm-XXXXXX";
        randname(name + sizeof(name) - 7);
        --retries;
        int shm_fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (shm_fd >= 0) {
            shm_unlink(name);
            return shm_fd;
        }
    } while (retries > 0 && errno == EEXIST);
    return -1;
}

static void buffer_handler(void *data, struct hyprland_toplevel_export_frame_v1 *frame, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    buffer_width = width;
    buffer_height = height;
    buffer_stride = stride;
    buffer_format = format;
    fprintf(stderr, "Got SHM buffer info: %ux%u, stride %u, format %u\n", width, height, stride, format);
}

static void frame_ready(void *data, struct hyprland_toplevel_export_frame_v1 *frame, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    fprintf(stderr, "Capture done. Writing buffer to file...\n");
    frame_done = 1;
}

static void frame_failed(void *data, struct hyprland_toplevel_export_frame_v1 *frame) {
    fprintf(stderr, "Frame copy failed\n");
    hyprland_toplevel_export_frame_v1_destroy(frame);
    wl_display_disconnect(display);
    exit(-1);
}

static void frame_linux_dmabuf(void *data, struct hyprland_toplevel_export_frame_v1 *frame, uint32_t format, uint32_t width, uint32_t height) {
    // no-op
}

static void frame_flags(void *data, struct hyprland_toplevel_export_frame_v1 *frame, uint32_t flags) {
    
}

static void frame_buffer_done(void *data, struct hyprland_toplevel_export_frame_v1 *frame) {
    // no-op
}

static void frame_damage(void *data, struct hyprland_toplevel_export_frame_v1 *frame, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    // no-op
}
static const struct hyprland_toplevel_export_frame_v1_listener frame_listener = {
    .buffer = buffer_handler,
    .ready = frame_ready,
    .failed = frame_failed,
    .linux_dmabuf = frame_linux_dmabuf,
    .flags = frame_flags,
    .buffer_done = frame_buffer_done,
    .damage = frame_damage,
};

static void registry_handler(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    // fprintf(stderr, "interface: '%s', version: %d, name: %d\n", interface, version, id);
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(registry, id, &wl_shm_interface, version);
    } else if (strcmp(interface, "hyprland_toplevel_export_manager_v1") == 0) {
        export_manager = wl_registry_bind(registry, id, &hyprland_toplevel_export_manager_v1_interface, 2); 
    }
}

static void registry_remover(void *data, struct wl_registry *registry, uint32_t id) {
    // no-op
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handler,
    .global_remove = registry_remover,
};

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <hex-window-address> <output-file-name>\n", argv[0]);
        return -1;
    }

    uint32_t window_handle;
    if (sscanf(argv[1], "%" SCNx32, &window_handle) != 1) {
        fprintf(stderr, "Invalid window address: %s\n", argv[1]);
        return -1;
    }

    const char *output_filename = argv[2];
    if (strlen(output_filename) > MAX_FILENAME_LENGTH) {
        fprintf(stderr, "Filename exceeds the maximum length (%d characters)\n", MAX_FILENAME_LENGTH);
        return -1;
    }

    display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "Failed to connect to the wayland display\n");
        return -1;
    }
    fprintf(stderr, "Connected to wayland display\n");
    
    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (export_manager == NULL) {
        fprintf(stderr, "Failed to bind to hyprland_toplevel_export_manager_v1 interface\n");
        return -1; 
    }

    // fprintf(stderr, "preparing to capture\n");
    export_frame = hyprland_toplevel_export_manager_v1_capture_toplevel(export_manager, 0, window_handle);
    
    if (!export_frame) {
        fprintf(stderr, "Failed to initiate a toplevel capture\n");
        wl_display_disconnect(display);
        return -1;
    }
    hyprland_toplevel_export_frame_v1_add_listener(export_frame, &frame_listener, NULL);
    while (!buffer_width && wl_display_dispatch(display) != -1) {
        // wait for buffer info
    }
    fprintf(stderr, "Creating wl_shm based buffer\n");

    int shm_fd = create_shm_file();
    if (shm_fd < 0) {
        perror("Failed to create shared memory file\n");
        return -1;
    }
    size_t size = buffer_stride * buffer_height;
    if (ftruncate(shm_fd, size) < 0) {
        perror("Failed to set size of shared memory file\n");
        close(shm_fd);
        return -1;
    }

    shm_data = mmap(NULL, size, PROT_READ, MAP_SHARED, shm_fd, 0);
    if (shm_data == MAP_FAILED) {
        perror("Failed to map shared memory file\n");
        close(shm_fd);
        return -1;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, shm_fd, size);
    buffer = wl_shm_pool_create_buffer(pool, 0, buffer_width, buffer_height, buffer_stride, buffer_format);
    wl_shm_pool_destroy(pool);
    close(shm_fd);
    
    fprintf(stderr, "Requesting image\n");

    hyprland_toplevel_export_frame_v1_copy(export_frame, buffer, 1);
    while (!frame_done && wl_display_dispatch(display) != -1) {
        // wait for copy to finish
    }

    fprintf(stderr, "Writing image\n");
    FILE *file = fopen(output_filename, "wb");
    if (!file) {
        perror("Failed to open output file");
    }

    // With PPM, alpha is not supported 
    // Write the PPM header:
    //   P6 -> binary PPM
    //   width height
    //   255 -> max color value
    fprintf(file, "P6\n%d %d\n255\n", buffer_width, buffer_height);

    for (int y = 0; y < buffer_height; y++) {
        const uint8_t *row_ptr = (const uint8_t *)shm_data + (y * buffer_stride);
        
        for (int x = 0; x < buffer_width; x++) {
            // XRGB8888
            uint8_t b = row_ptr[x * 4 + 0];
            uint8_t g = row_ptr[x * 4 + 1];
            uint8_t r = row_ptr[x * 4 + 2];
            // uint8_t a = row_ptr[x * 4 + 3];

            // if (buffer_format == WL_SHM_FORMAT_ARGB8888) {}
                
            fputc(r, file);
            fputc(g, file);
            fputc(b, file);
        }
    }
    // fwrite(shm_data, buffer_stride * buffer_height, 1, file);
    
    /*
    // TGA Header (18 bytes) for uncompressed 32-bit image.
    uint8_t header[18] = {0};

    // header[2] = 2 => Uncompressed true-color image
    header[2] = 2;
    // Image width (low byte, high byte)
    header[12] = (uint8_t)(buffer_width& 0xFF);
    header[13] = (uint8_t)((buffer_width>> 8) & 0xFF);
    // Image height (low byte, high byte)
    header[14] = (uint8_t)(buffer_height & 0xFF);
    header[15] = (uint8_t)((buffer_height >> 8) & 0xFF);
    // Bits per pixel
    header[16] = 32;
    // Image descriptor: bit 5 set => top-left origin (if desired),
    // but for a bottom-left origin, leave this at 0 or set bits accordingly.
    header[17] = 0; // or 0x20 for top-left

    // Write header.
    fwrite(header, 1, 18, file);

    // Write the pixel data. 
    // Adjust as needed if your pixel data is top-left first vs bottom-left.
    fwrite(shm_data, 4, (size_t)buffer_width * buffer_height, file);

    // The TGA footer/extension area is optional for basic usage. 
    */
    fclose(file);

    fprintf(stderr, "Image data written to %s\n", output_filename);

    hyprland_toplevel_export_frame_v1_destroy(export_frame);

    wl_display_disconnect(display);
    return 0;
}
