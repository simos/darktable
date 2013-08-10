#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include "common/darktable.h"
#include "common/exif.h"
#include "common/imageio.h"
#include "common/imageio_module.h"
#include "common/imageio_format.h"
#include "dtgtk/slider.h"
#include "dtgtk/togglebutton.h"

#include <webp/encode.h>
#include <webp/types.h>

DT_MODULE(1)

typedef struct dt_imageio_webp_t
{
  int max_width, max_height;
  int width, heigth;
  char style[128];
}
dt_imageio_webp_t;

typedef struct dt_imageio_webp_gui_data_t
{
  GtkDarktableToggleButton *lossy, *lossless;
  GtkComboBox *preset;
  GtkDarktableSlider *quality;
}
dt_imageio_webp_gui_data;

void init(dt_imageio_module_format_t *self) {}
void cleanup(dt_imageio_module_format_t *self) {}

int
write_image (dt_imageio_module_data_t *webp, const char *filename, const void *in_tmp, void *exif, int exif_len, int imgid)
{
  //WRITE_THIS
  return 0;
}

size_t
params_size(dt_imageio_module_format_t *self)
{
  return sizeof(dt_imageio_module_data_t);
}

void*
get_params(dt_imageio_module_format_t *self)
{
  dt_imageio_module_data_t *d = (dt_imageio_module_data_t *)malloc(sizeof(dt_imageio_module_data_t));
  memset(d,0,sizeof(dt_imageio_module_data_t));
  return d;
}

void
free_params(dt_imageio_module_format_t *self, dt_imageio_module_data_t *params)
{
  free(params);
}

int
set_params(dt_imageio_module_format_t *self, const void *params, const int size)
{
  if(size != self->params_size(self)) return 1;
  //dt_imageio_jpeg_t *d = (dt_imageio_jpeg_t *)params;
  //dt_imageio_jpeg_gui_data_t *g = (dt_imageio_jpeg_gui_data_t *)self->gui_data;
  //dtgtk_slider_set_value(g->quality, d->quality);
  return 0;
}


int
bpp(dt_imageio_module_data_t *p)
{
  return 8;
}

int
levels(dt_imageio_module_data_t *p)
{
  return IMAGEIO_RGB | IMAGEIO_INT8;
}

const char*
mime(dt_imageio_module_data_t *data)
{
  // TODO: revisit this when IANA makes it official.
  return "image/webp";
}

const char*
extension(dt_imageio_module_data_t *data)
{
  return "webp";
}

int
dimension (dt_imageio_module_format_t *self, uint32_t *width, uint32_t *height)
{
  return 0;
}

const char*
name ()
{
  return _("WebP");
}

void gui_init (dt_imageio_module_format_t *self) {}
void gui_cleanup (dt_imageio_module_format_t *self) {}
void gui_reset (dt_imageio_module_format_t *self) {}

int flags (dt_imageio_module_data_t *data)
{
  return FORMAT_FLAGS_SUPPORT_XMP;
}
