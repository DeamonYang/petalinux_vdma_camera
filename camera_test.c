
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <pthread.h>
#include <linux/input.h>

#define VIDEO_DEV			"/dev/video0"
#define FB_DEV				"/dev/fb0"
#define INPUT_DEV			"/dev/input/event0"
#define VIDEO_BUFFER_COUNT	3

unsigned char base[1024*768*3];

struct buffer_info {
	void *start;
	unsigned int length;
};

int main(int argc, char **argv)
{
	int fb_fd = -1;
	int video_fd = -1;
	struct fb_var_screeninfo fb_var = {0};
	struct fb_fix_screeninfo fb_fix = {0};
	unsigned char *base;
	unsigned int screensize=10;
	struct v4l2_format fmt = {0};
	struct v4l2_requestbuffers req_bufs = {0};
	static struct v4l2_buffer buf;
	int n_buf;
	struct buffer_info bufs_info[VIDEO_BUFFER_COUNT];
	enum v4l2_buf_type type;

//	/* ¿?¿?framebuffer¿?¿? */
//	fb_fd = open(FB_DEV, O_RDWR);
//	if (0 > fb_fd) {
//		printf("ERROR: failed to open framebuffer device %s\n", FB_DEV);
//		return (void *)-1;
//	}
//
//	/* ¿?¿?framebuffer¿?¿?¿?¿? */
//	if (0 > ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_var)) {
//		printf("ERROR: failed to FBIOGET_VSCREENINFO\n");
//		return (void *)-1;
//	}
//
//	if (0 > ioctl(fb_fd, FBIOGET_FSCREENINFO, &fb_fix)) {
//		printf("ERROR: failed to FBIOGET_FSCREENINFO\n");
//		return (void *)-1;
//	}
//
//	/* ¿?¿?¿?¿? */
//	screensize = fb_var.yres * fb_fix.line_length;
//	base = (unsigned char *)mmap(NULL, screensize,
//				PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
//	if (MAP_FAILED == base) {
//		printf("ERROR: failed to mmap framebuffer, size 0x%x\n", screensize);
//		return (void *)-1;
//	}

	/* ¿?¿?¿?¿?¿?¿?¿? */
	video_fd = open(VIDEO_DEV, O_RDWR);
	if (0 > video_fd) {
		printf("ERROR: failed to open video device %s\n", VIDEO_DEV);
		return (void *)-1;
	}

	/* ¿?¿?¿?¿?¿?¿?¿? */
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = 800;//fb_var.xres;
	fmt.fmt.pix.height = 480;//fb_var.yres;
	fmt.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;

	if (0 > ioctl(video_fd, VIDIOC_S_FMT, &fmt)) {
		printf("ERROR: failed to VIDIOC_S_FMT\n");
		return (void *)-1;
	}

	/* ¿?¿?buffer */
	req_bufs.count = VIDEO_BUFFER_COUNT;
	req_bufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req_bufs.memory = V4L2_MEMORY_MMAP;

	if (0 > ioctl(video_fd, VIDIOC_REQBUFS, &req_bufs)) {
		printf("ERROR: failed to VIDIOC_REQBUFS\n");
		return (void *)-1;
	}

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	for (n_buf = 0; n_buf < VIDEO_BUFFER_COUNT; n_buf++) {

		buf.index = n_buf;
		if (0 > ioctl(video_fd, VIDIOC_QUERYBUF, &buf)) {
			printf("ERROR: failed to VIDIOC_QUERYBUF\n");
			return (void *)-1;
		}

		bufs_info[n_buf].length = buf.length;
		bufs_info[n_buf].start = mmap(NULL, buf.length,
					PROT_READ | PROT_WRITE, MAP_SHARED,
					video_fd, buf.m.offset);
		if (MAP_FAILED == bufs_info[n_buf].start) {
			printf("ERROR: failed to mmap video buffer, size 0x%x\n", buf.length);
			return (void *)-1;
		}
	}

	/* buffer¿?¿? */
	for (n_buf = 0; n_buf < VIDEO_BUFFER_COUNT; n_buf++) {

		buf.index = n_buf;
		if (0 > ioctl(video_fd, VIDIOC_QBUF, &buf)) {
			printf("ERROR: failed to VIDIOC_QBUF\n");
			return (void *)-1;
		}
	}

	/* ¿?¿?¿?¿? */
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (0 > ioctl(video_fd, VIDIOC_STREAMON, &type)) {
		printf("ERROR: failed to VIDIOC_STREAMON\n");
		return (void *)-1;
	}
	printf("start\n");
	for ( ; ; ) {

		for(n_buf = 0; n_buf < VIDEO_BUFFER_COUNT; n_buf++) {

			printf("cap\n");
			buf.index = n_buf;
			if (0 > ioctl(video_fd, VIDIOC_DQBUF, &buf)) {
				printf("ERROR: failed to VIDIOC_DQBUF\n");
				return (void *)-1;
			}

			printf("memcpy\n");
		//	memcpy(base, (unsigned char *)bufs_info[n_buf].start, screensize);

			if (0 > ioctl(video_fd, VIDIOC_QBUF, &buf)) {
				printf("ERROR: failed to VIDIOC_QBUF\n");
				return (void *)-1;
			}
		}
		printf("pass\n");
	}

	return (void *)0;;
}





