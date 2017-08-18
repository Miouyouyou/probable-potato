#include <sys/ioctl.h>

// fprintf
#include <stdio.h>

// close
#include <unistd.h>

#include <stdint.h>

// struct drm_mode_create_dumb
#include <libdrm/drm.h>

// drmModeGetResources -- Does not depend on X11
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <errno.h>

#include "common-drm-functions.h"

#define LOG(msg, ...) \
	fprintf(\
		stderr, "[%s (%s:%d)] "msg,\
		__func__, __FILE__, __LINE__, ##__VA_ARGS__ \
	)

// -- UNTESTED FUNCTIONS
drmModeModeInfo * choose_preferred_resolution_on_connector
(drmModeConnector * __restrict connector)
{
	drmModeModeInfo * best_resolution = NULL;

	for (int_fast32_t m = 0; m < connector->count_modes; m++) {
		drmModeModeInfo * __restrict tested_resolution =
			&connector->modes[m];
		if (tested_resolution->type & DRM_MODE_TYPE_PREFERRED) {
			best_resolution = tested_resolution;
			break;
		}
	}

	return best_resolution;
}
int drm_get_best_output(int drm_fd)
{
	/* DRM is based on the fact that you can connect multiple screens,
	 * on multiple different connectors, which have, of course, multiple
	 * encoders that transform the ARGB8888 or XRGB8888 framebuffer used
	 * in the user application into something the selected screen
	 * comprehend, before sending it.
	 * 
	 * The default selection system is simple :
	 * - We try to find the first connected screen that accept our
	 *   lowest resolution (1280x720 32BPP)
	 */
	drmModeRes         * __restrict drm_resources;
	drmModeConnector   * __restrict valid_connector   = NULL;
	drmModeModeInfo    * __restrict chosen_resolution = NULL;
	drmModeEncoder     * __restrict screen_encoder    = NULL;
	uint32_t crtc_id;
	struct modeset_dev * __restrict device;
	int ret = 0;

	/* Let's see what we can use through this drm node */
	drm_resources = drmModeGetResources(drm_fd);

	for (int_fast32_t c = 0; c < drm_resources->count_connectors; c++) {
		valid_connector =
			drmModeGetConnector(drm_fd, drm_resources->connectors[c]);

		if (valid_connector->connection == DRM_MODE_CONNECTED)
			break;

		drmModeFreeConnector(valid_connector);
		valid_connector = NULL;
	}

	if (!valid_connector) {
		/* Then there was no connectors,
		 * or no connector were connected */
		LOG("No connectors or no connected connectors found...\n");
		ret = -ENOLINK;
		goto no_valid_connector;
	}

	chosen_resolution =
		choose_preferred_resolution_on_connector(valid_connector);
	
	if (!chosen_resolution) {
		LOG(
			"No preferred resolution on the selected connector %u ?\n",
			valid_connector->connector_id
		);
		ret = -ENAVAIL;
		goto no_valid_resolution;
	}

	screen_encoder =
		drmModeGetEncoder(drm_fd, valid_connector->encoder_id);
	
	if (!screen_encoder) {
		LOG(
			"Could not retrieve the encoder for mode %s on connector %u.\n",
			chosen_resolution->name,
			valid_connector->connector_id
		);
		ret = -ENOLINK;
		goto could_not_retrieve_encoder;
	}

	crtc_id = screen_encoder->crtc_id;

	return ret;

could_not_retrieve_encoder:
	drmModeFreeModeInfo(chosen_resolution);
no_valid_resolution:
	drmModeFreeConnector(valid_connector);
no_valid_connector:
	return ret;
}

// -- TESTED FUNCTIONS
int main() {
	char const * __restrict const drm_hardware_dev_node_path =
		"/dev/dri/card0";
	uint32_t dumb_buffer_handle;
	int const drm_fd   = drm_open_node(drm_hardware_dev_node_path);
	int prime_fd = -1;

	if (drm_fd < 0) {
		/* %m is actually a GLIBC specific format for strerror(errno) */
		LOG(
			"Could not open %s with Read-Write rights : %m\n",
			drm_hardware_dev_node_path
		);
		goto could_not_open_drm_fd;
	}

	if (!drm_node_support_dumb_buffers(drm_fd)) {
		LOG(
			"Looks like %s do not support Dumb Buffers...\n",
			drm_hardware_dev_node_path
		);
		goto dumb_buffers_not_supported;
	}

	if (allocate_drm_dumb_buffer(
	      drm_fd,1280,720,32,&dumb_buffer_handle) < 0) {
		LOG("Could not allocate a 1280x720@32bpp frame buffer... %m\n");
		goto could_not_allocate_drm_dumb_buffer;
	}

	prime_fd = 
		drm_convert_buffer_handle_to_prime_fd(drm_fd, dumb_buffer_handle);
	if (prime_fd < 0) {
		LOG("Unable to convert the GEM handle to a DRM Handle : %m\n");
		goto could_not_export_dumb_buffer;
	}

	LOG("Got a Prime buffer FD : %d ! Yay !\n", prime_fd);

could_not_export_dumb_buffer:
	free_drm_dumb_buffer(drm_fd, dumb_buffer_handle);
could_not_allocate_drm_dumb_buffer:
dumb_buffers_not_supported:
	close(drm_fd);
could_not_open_drm_fd:
	return 0;
}
