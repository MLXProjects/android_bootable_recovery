#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "minui.h"
#include "graphics.h"
#include <pixelflinger/pixelflinger.h>

typedef struct {
	int w;
	int h;
	uint8_t alpha;
	uint8_t hicolor;
} ASHMEM_CANVAS, * ASHMEM_CANVASP; 

static GRSurface* mmap_init(minui_backend*);
static GRSurface* mmap_flip(minui_backend*);
static void mmap_blank(minui_backend*, bool);
static void mmap_exit(minui_backend*);

static GRSurface gr_framebuffer[1];
static ASHMEM_CANVASP ashmem = NULL;
static GRSurface* gr_draw = NULL;

static int fb_fd = -1;
static char *mmap_path = NULL;
static __u32 smem_len;

static minui_backend my_backend = {
    .init = mmap_init,
    .flip = mmap_flip,
    .blank = mmap_blank,
    .exit = mmap_exit,
};

minui_backend* open_mmap() {
    return &my_backend;
}

void gr_mmapfd(char *path){
	mmap_path=path;
}

static void mmap_blank(minui_backend* backend __unused, bool blank)
{
	/* TODO: redirect screen brightness to canvas opacity -> draw to black */
#if defined(TW_BRIGHTNESS_PATH) && defined(TW_MAX_BRIGHTNESS)
    int fd;
    char brightness[4];
    snprintf(brightness, 4, "%03d", TW_MAX_BRIGHTNESS/2);

    fd = open(TW_BRIGHTNESS_PATH, O_RDWR);
    if (fd < 0) {
        printf("cannot open LCD backlight\n");
        return;
    }
    write(fd, blank ? "000" : brightness, 3);
    close(fd);
#endif
}

static GRSurface* mmap_init(minui_backend* backend) {
	
	/* load fd from argv */
	int fd = open(mmap_path, O_RDWR, 0666);
	if (fd<0){
		printf("invalid mmap path=%s\n", mmap_path);
		return NULL;
	}
	printf("mmap detected at %d\n", fd);

    void* bits = mmap(0, sizeof(ASHMEM_CANVAS), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bits == MAP_FAILED) {
        printf("failed to mmap canvas data - first\n");
        close(fd);
        return NULL;
    }
	printf("mmap done @%p\n", bits);
	
	ASHMEM_CANVASP temp_cv = (ASHMEM_CANVASP) malloc(sizeof(ASHMEM_CANVAS));
	if (!temp_cv){
		printf("allocate temp struct failed\n");
		munmap(bits, sizeof(ASHMEM_CANVAS));
		close(fd);
		return NULL;
	}
	
	printf("clone mmapped data to temp struct\n");
	ashmem = (ASHMEM_CANVASP) bits;
	memcpy(temp_cv, ashmem, sizeof(ASHMEM_CANVAS));
	printf("clone mmapped data to temp struct done\n");
	
    // We print this out for informational purposes only, but
    // throughout we assume that the framebuffer device uses an RGBX
    // pixel format.  This is the case for every development device I
    // have access to.  For some of those devices (eg, hammerhead aka
    // Nexus 5), FBIOGET_VSCREENINFO *reports* that it wants a
    // different format (XBGR) but actually produces the correct
    // results on the display when you write RGBX.
    //
    // If you have a device that actually *needs* another pixel format
    // (ie, BGRX, or 565), patches welcome...

    printf("ashmem reports (must be accurate):\n"
           "  canvas width    = %d \theight = %d\n"
           "  canvas alpha    = %d \thicolor= %d\n",
		   temp_cv->w, temp_cv->h,
		   temp_cv->alpha, temp_cv->hicolor
    );
	
	munmap(bits, sizeof(ASHMEM_CANVAS));
	printf("mmap again but entire size\n");
	int sz = ((temp_cv->w*temp_cv->h)*2)+sizeof(ASHMEM_CANVAS);
	smem_len = (__u32) sz;
    bits = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bits == MAP_FAILED) {
        printf("failed to mmap entire framebuffer\n");
        close(fd);
        return NULL;
    }
	printf("buffer map done\n");
	
	ashmem = (ASHMEM_CANVASP) bits;
	
	printf("free temp struct\n");
	free(temp_cv);
	printf("free temp struct done\n");

    //memset(bits, 0, smem_len);

    gr_framebuffer[0].width = ashmem->w;
    gr_framebuffer[0].height = ashmem->h;
    gr_framebuffer[0].row_bytes = sz/ashmem->h;
    gr_framebuffer[0].pixel_bytes = gr_framebuffer[0].row_bytes / ashmem->w;
    gr_framebuffer[0].data = reinterpret_cast<uint8_t*>(bits);
    gr_framebuffer[0].data += sizeof(ASHMEM_CANVAS);
	printf("No valid pixel format detected, trying GGL_PIXEL_FORMAT_RGB_565\n");
	gr_framebuffer[0].format = GGL_PIXEL_FORMAT_RGB_565;

    // Drawing directly to the framebuffer takes about 5 times longer.
    // Instead, we will allocate some memory and draw to that, then
    // memcpy the data into the framebuffer later.
    gr_draw = (GRSurface*) malloc(sizeof(GRSurface));
    if (!gr_draw) {
        printf("failed to allocate gr_draw\n");
        close(fd);
        munmap(bits, smem_len);
        return NULL;
    }
	printf("2nd surface allocate done\n");
    memcpy(gr_draw, gr_framebuffer, sizeof(GRSurface));
    gr_draw->data = (unsigned char*) calloc(gr_draw->height * gr_draw->row_bytes, 1);
    if (!gr_draw->data) {
        printf("failed to allocate in-memory surface\n");
        close(fd);
        free(gr_draw);
        munmap(bits, smem_len);
        return NULL;
    }
	
	printf("single buffered\n");
	
    fb_fd = fd;

    printf("framebuffer: %d (%d x %d)\n", fb_fd, gr_draw->width, gr_draw->height);

    mmap_blank(backend, true);
    mmap_blank(backend, false);

    return gr_draw;
}

static GRSurface* mmap_flip(minui_backend* backend __unused) {
	// Copy from the in-memory surface to the framebuffer.
	memcpy(gr_framebuffer[0].data, gr_draw->data,
		   gr_draw->height * gr_draw->row_bytes);
	
    return gr_draw;
}

static void mmap_exit(minui_backend* backend __unused) {
    munmap(ashmem, smem_len+sizeof(ASHMEM_CANVAS));
    close(fb_fd);
    fb_fd = -1;

    if (gr_draw) {
        free(gr_draw->data);
        free(gr_draw);
    }
    gr_draw = NULL;
}
