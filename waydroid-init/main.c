#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/limits.h>
#include <utils/Log.h>
#include <cutils/properties.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "waydroid-init"
#define DMABUF_SYSTEM_HEAP "/dev/dma_heap/system"

int get_gpu_kernel_driver_name(char *render_node, char *driver_name, size_t driver_name_maxlen) {
  char driver_symlink_path[PATH_MAX], driver_path[PATH_MAX] = {'\0'};
  int len;
  struct stat node_info;

  if (stat(render_node, &node_info) != 0) {
    ALOGE("Failed to access GPU render node: %s", strerror(errno));
    return -errno;
  }

  if (!S_ISCHR(node_info.st_mode)) {
    ALOGE("GPU render node is not a character device");
    return -1;
  }

  snprintf(driver_symlink_path, PATH_MAX, "/sys/dev/char/%i:%i/device/driver", major(node_info.st_rdev), minor(node_info.st_rdev));

  len = readlink(driver_symlink_path, driver_path, PATH_MAX - 1);

  if (len == -1) {
    ALOGE("Failed to read GPU driver name: %s", strerror(errno));
    return -errno;
  }

  strncpy(driver_name, basename(driver_path), driver_name_maxlen);
  return 0;
}

int main(int argc, char **argv) {
  bool override_gralloc = property_get_bool("gralloc.override", true);
  char gralloc_impl[PROPERTY_VALUE_MAX],
       render_node[PROPERTY_VALUE_MAX],
       gpu_driver_name[20];
  int  ret;

  property_get("gralloc.gbm.device", render_node, "/dev/dri/renderD128");
  ALOGI("Using GPU device %s", render_node);

  if (access(render_node, F_OK) != 0) {
    ALOGE("GPU device %s does not exist!", render_node);
    return 1;
  }

  ret = get_gpu_kernel_driver_name(render_node, gpu_driver_name, sizeof(gpu_driver_name));
  if (ret != 0) return ret;

  ALOGI("GPU kernel driver: %s", gpu_driver_name);

  if (override_gralloc) {
    if (strcmp(gpu_driver_name, "amdgpu") == 0) {
      strcpy(gralloc_impl, "minigbm_amdgpu");
    } else if (strcmp(gpu_driver_name, "i915") == 0 || strcmp(gpu_driver_name, "xe") == 0) {
      strcpy(gralloc_impl, "minigbm_intel");
    } else if (strncmp(gpu_driver_name, "virtio", 6) == 0) {
      strcpy(gralloc_impl, "minigbm_generic");
    } else if (strncmp(gpu_driver_name, "vmwgfx", 6) == 0) {
      strcpy(gralloc_impl, "minigbm_vmwgfx");
    } else {
      strcpy(gralloc_impl, "minigbm_gbm_mesa");
    }
  } else {
    property_get("ro.hardware.gralloc", gralloc_impl, "minigbm_gbm_mesa");
  }

  // gbm/minigbm_gbm_mesa does not support YUV
  if (strcmp(gralloc_impl, "minigbm_intel") == 0 ||
      strcmp(gralloc_impl, "minigbm_amdgpu") == 0 ||
      strcmp(gralloc_impl, "minigbm_generic") == 0) {

    property_set("persist.ffmpeg-codec2.pixel_format", "YUV_420");
  } else {
    property_set("persist.ffmpeg-codec2.pixel_format", "RGBX_8888");
  }

  ALOGI("Using gralloc implementation: %s", gralloc_impl);
  property_set("ro.hardware.gralloc", gralloc_impl);
  property_set("gralloc.minigbm.impl", gralloc_impl);

  if (access(DMABUF_SYSTEM_HEAP, F_OK) != 0) {
    ALOGE("DMA-BUF system heap does not exist, video playback might not work properly");
    ALOGE("Falling back to gralloc");
    property_set("debug.stagefright.c2-poolmask", "0xfc0000");
  }
}
