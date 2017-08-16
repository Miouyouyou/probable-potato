#include <sys/ioctl.h>

#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>

#include <errno.h>

// memset
#include <string.h>

// open
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// fprintf
#include <stdio.h>

// close
#include <unistd.h>

#include <stdint.h>

static int allocate_drm_dumb_buffer
(int const drm_fd,
 uint32_t const width, uint32_t const height, uint32_t const bpp,
 struct drm_mode_create_dumb * __restrict const dumb_buffer)
{
	int ret = 0;

	memset(dumb_buffer, 0, sizeof(struct drm_mode_create_dumb));

	dumb_buffer->width  = width;
	dumb_buffer->height = height;
	dumb_buffer->bpp   = bpp;

	if (ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &dumb_buffer) < 0)
		ret = -errno;

	return ret;
}

static void free_dumb_buffer
(int const drm_fd, uint32_t const dumb_handle)
{
	struct drm_mode_destroy_dumb request_destroy = {
		.handle = dumb_handle
	};

	ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &request_destroy);
}

static int drm_open_node(char const * __restrict const dev_node_path) {
	return open(dev_node_path, O_RDWR);
}

static int drm_convert_buffer_handle_to_prime_fd
(int const drm_fd, int const gem_handle)
{
	struct drm_prime_handle prime_structure;
	
	memset(&prime_structure, 0, sizeof(prime_structure));
	prime_structure.handle = gem_handle;
	prime_structure.flags  = DRM_CLOEXEC;
	prime_structure.fd     = -1;

	ioctl(drm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime_structure);

	return prime_structure.fd;
}

int main() {
	char const * __restrict const drm_hardware_dev_node_path =
		"/dev/dri/card0";
	struct drm_mode_create_dumb dumb_buffer = {0};
	int const drm_fd   = drm_open_node(drm_hardware_dev_node_path);
	int prime_fd = -1;
	
	if (drm_fd < 0) {
		/* %m is actually a GLIBC specific format for strerror(errno) */
		fprintf(
			stderr, "Could not open %s with Read-Write rights : %m\n",
			drm_hardware_dev_node_path
		);
		goto could_not_open_drm_fd;
	}

	if (allocate_drm_dumb_buffer(drm_fd,1280,720,32,&dumb_buffer) < 0) {
		fprintf(
			stderr, "Could not allocate a 1280x720@32bpp frame buffer... %m\n"
		);
		goto could_not_allocate_drm_dumb_buffer;
	}

	prime_fd = 
		drm_convert_buffer_handle_to_prime_fd(drm_fd, dumb_buffer.handle);
	if (prime_fd) {
		fprintf(
			stderr, "Unable to convert the GEM Buffer to a DRM Handle ?? %m\n"
		);
		goto could_not_export_dumb_buffer;
	}

	fprintf(stderr, "Got a Prime buffer FD : %d ! Yay !", prime_fd);

could_not_export_dumb_buffer:
	free_dumb_buffer(drm_fd, dumb_buffer.handle);
could_not_allocate_drm_dumb_buffer:
	close(drm_fd);
could_not_open_drm_fd:
	return 0;
}
