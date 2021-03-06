/*
		This file is part of darktable,
		copyright (c) 2011--2012 ulrich pegelow.

		darktable is free software: you can redistribute it and/or modify
		it under the terms of the GNU General Public License as published by
		the Free Software Foundation, either version 3 of the License, or
		(at your option) any later version.

		darktable is distributed in the hope that it will be useful,
		but WITHOUT ANY WARRANTY; without even the implied warranty of
		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
		GNU General Public License for more details.

		You should have received a copy of the GNU General Public License
		along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include "bauhaus/bauhaus.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "develop/tiling.h"
#include "control/control.h"
#include "common/debug.h"
#include "common/opencl.h"
#include "common/gaussian.h"
#include "common/bilateral.h"
#include "common/bilateralcl.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "gui/presets.h"
#include <gtk/gtk.h>
#include <inttypes.h>
#include <xmmintrin.h>

#define CLAMPF(a, mn, mx)       ((a) < (mn) ? (mn) : ((a) > (mx) ? (mx) : (a)))
#define MMCLAMPPS(a, mn, mx)    (_mm_min_ps((mx), _mm_max_ps((a), (mn))))

#define BLOCKSIZE 64		/* maximum blocksize. must be a power of 2 and will be automatically reduced if needed */

DT_MODULE(1)

typedef struct dt_iop_lowpass_params_t
{
  dt_gaussian_order_t order;
  float radius;
  float contrast;
  float saturation;
}
dt_iop_lowpass_params_t;

typedef struct dt_iop_lowpass_gui_data_t
{
  GtkWidget *scale1,*scale2,*scale3;       // radius, contrast, saturation
  GtkWidget *order;			    // order of gaussian
  GtkWidget *bilat;
}
dt_iop_lowpass_gui_data_t;

typedef struct dt_iop_lowpass_data_t
{
  dt_gaussian_order_t order;
  float radius;
  float contrast;
  float saturation;
  float table[0x10000];        // precomputed look-up table for contrast curve
  float unbounded_coeffs[3];   // approximation for extrapolation
}
dt_iop_lowpass_data_t;

typedef struct dt_iop_lowpass_global_data_t
{
  int kernel_lowpass_mix;
}
dt_iop_lowpass_global_data_t;


const char *name()
{
  return _("lowpass");
}

int flags()
{
  return IOP_FLAGS_INCLUDE_IN_STYLES | IOP_FLAGS_SUPPORTS_BLENDING | IOP_FLAGS_ALLOW_TILING;
}

int
groups ()
{
  return IOP_GROUP_EFFECT;
}

void init_key_accels(dt_iop_module_so_t *self)
{
  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "radius"));
  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "contrast"));
  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "saturation"));
}

void connect_key_accels(dt_iop_module_t *self)
{
  dt_iop_lowpass_gui_data_t *g = (dt_iop_lowpass_gui_data_t*)self->gui_data;

  dt_accel_connect_slider_iop(self, "radius", GTK_WIDGET(g->scale1));
  dt_accel_connect_slider_iop(self, "contrast", GTK_WIDGET(g->scale2));
  dt_accel_connect_slider_iop(self, "saturation", GTK_WIDGET(g->scale3));
}


#ifdef HAVE_OPENCL
int
process_cl (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, cl_mem dev_in, cl_mem dev_out, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{
  dt_iop_lowpass_data_t *d = (dt_iop_lowpass_data_t *)piece->data;
  dt_iop_lowpass_global_data_t *gd = (dt_iop_lowpass_global_data_t *)self->data;

  cl_int err = -999;
  const int devid = piece->pipe->devid;

  const int width = roi_in->width;
  const int height = roi_in->height;
  const int channels = piece->colors;

  const float Labmax[] = { 100.0f, 128.0f, 128.0f, 1.0f };
  const float Labmin[] = { 0.0f, -128.0f, -128.0f, 0.0f };

  const int   use_bilateral = d->radius < 0 ? 1 : 0;
  const float radius = fmax(0.1f, fabs(d->radius));
  const float sigma = radius * roi_in->scale / piece ->iscale;
  const float saturation = d->saturation;
  const int order = d->order;

  size_t sizes[3];

  cl_mem dev_m = NULL;
  cl_mem dev_coeffs = NULL;

  dt_gaussian_cl_t *g = NULL;
  dt_bilateral_cl_t *b = NULL;

  if(!use_bilateral)
  {
    g = dt_gaussian_init_cl(devid, width, height, channels, Labmax, Labmin, sigma, order);
    if(!g) goto error;
    err = dt_gaussian_blur_cl(g, dev_in, dev_out);
    if(err != CL_SUCCESS) goto error;
    dt_gaussian_free_cl(g);
    g = NULL;
  }
  else
  {
    const float sigma_r = 100.0f; // does not depend on scale
    const float sigma_s = sigma;
    const float detail = -1.0f; // we want the bilateral base layer

    b = dt_bilateral_init_cl(devid, width, height, sigma_s, sigma_r);
    if(!b) goto error;
    err = dt_bilateral_splat_cl(b, dev_in);
    if (err != CL_SUCCESS) goto error;
    err = dt_bilateral_blur_cl(b);
    if (err != CL_SUCCESS) goto error;
    err = dt_bilateral_slice_cl(b, dev_in, dev_out, detail);
    if (err != CL_SUCCESS) goto error;
    dt_bilateral_free_cl(b);
    b = NULL; // make sure we don't clean it up twice
  }

  dev_m = dt_opencl_copy_host_to_device(devid, d->table, 256, 256, sizeof(float));
  if(dev_m == NULL) goto error;
  dev_coeffs = dt_opencl_copy_host_to_device_constant(devid, sizeof(float)*3, d->unbounded_coeffs);
  if(dev_coeffs == NULL) goto error;

  sizes[0] = ROUNDUPWD(width);
  sizes[1] = ROUNDUPWD(height);
  sizes[2] = 1;
  dt_opencl_set_kernel_arg(devid, gd->kernel_lowpass_mix, 0, sizeof(cl_mem), (void *)&dev_out);
  dt_opencl_set_kernel_arg(devid, gd->kernel_lowpass_mix, 1, sizeof(cl_mem), (void *)&dev_out);
  dt_opencl_set_kernel_arg(devid, gd->kernel_lowpass_mix, 2, sizeof(int), (void *)&width);
  dt_opencl_set_kernel_arg(devid, gd->kernel_lowpass_mix, 3, sizeof(int), (void *)&height);
  dt_opencl_set_kernel_arg(devid, gd->kernel_lowpass_mix, 4, sizeof(float), (void *)&saturation);
  dt_opencl_set_kernel_arg(devid, gd->kernel_lowpass_mix, 5, sizeof(cl_mem), (void *)&dev_m);
  dt_opencl_set_kernel_arg(devid, gd->kernel_lowpass_mix, 6, sizeof(cl_mem), (void *)&dev_coeffs);
  err = dt_opencl_enqueue_kernel_2d(devid, gd->kernel_lowpass_mix, sizes);
  if(err != CL_SUCCESS) goto error;

  if (dev_coeffs != NULL) dt_opencl_release_mem_object(dev_coeffs);
  if (dev_m != NULL) dt_opencl_release_mem_object(dev_m);

  return TRUE;

error:
  if (g) dt_gaussian_free_cl(g);
  if (b) dt_bilateral_free_cl(b);
  if (dev_coeffs != NULL) dt_opencl_release_mem_object(dev_coeffs);
  if (dev_m != NULL) dt_opencl_release_mem_object(dev_m);
  dt_print(DT_DEBUG_OPENCL, "[opencl_lowpass] couldn't enqueue kernel! %d\n", err);
  return FALSE;
}
#endif

void tiling_callback  (struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out, struct dt_develop_tiling_t *tiling)
{
  dt_iop_lowpass_data_t *d = (dt_iop_lowpass_data_t *)piece->data;

  const float radius = fmax(0.1f, fabs(d->radius));
  const float sigma = radius * roi_in->scale / piece ->iscale;
  const float sigma_r = 100.0f; // does not depend on scale
  const float sigma_s = sigma;

  const int width = roi_in->width;
  const int height = roi_in->height;
  const int channels = piece->colors;

  const size_t basebuffer = width*height*channels*sizeof(float);

  if(d->radius < 0.0f)
  {
    // bilateral filter
    tiling->factor = 2.0f + (float)dt_bilateral_memory_use(width,height,sigma_s,sigma_r)/basebuffer;
    tiling->maxbuf = fmax(1.0f, (float)dt_bilateral_singlebuffer_size(width,height,sigma_s,sigma_r)/basebuffer);
  }
  else
  {
    // gaussian blur
    tiling->factor = 2.0f + (float)dt_gaussian_memory_use(width, height, channels)/basebuffer;
    tiling->maxbuf = fmax(1.0f, (float)dt_gaussian_singlebuffer_size(width, height, channels)/basebuffer);
  }
  tiling->overhead = 0;
  tiling->overlap = ceilf(4*sigma);
  tiling->xalign = 1;
  tiling->yalign = 1;
  return;
}

void process (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, void *ivoid, void *ovoid, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{
  dt_iop_lowpass_data_t *data = (dt_iop_lowpass_data_t *)piece->data;
  float *in  = (float *)ivoid;
  float *out = (float *)ovoid;


  const int width = roi_in->width;
  const int height = roi_in->height;
  const int ch = piece->colors;

  const int   use_bilateral = data->radius < 0 ? 1 : 0;
  const float radius = fmax(0.1f, fabs(data->radius));
  const float sigma = radius * roi_in->scale / piece ->iscale;
  const int order = data->order;

  const float Labmax[] = { 100.0f, 128.0f, 128.0f, 1.0f };
  const float Labmin[] = { 0.0f, -128.0f, -128.0f, 0.0f };


  if(!use_bilateral)
  {
    dt_gaussian_t *g = dt_gaussian_init(width, height, ch, Labmax, Labmin, sigma, order);
    if(!g) return;
    dt_gaussian_blur_4c(g, in, out);
    dt_gaussian_free(g);
  }
  else
  {
    const float sigma_r = 100.0f;// d->sigma_r; // does not depend on scale
    const float sigma_s = sigma;
    const float detail = -1.0f; // we want the bilateral base layer

    dt_bilateral_t *b = dt_bilateral_init(width, height, sigma_s, sigma_r);
    if(!b) return;
    dt_bilateral_splat(b, in);
    dt_bilateral_blur(b);
    dt_bilateral_slice(b, in, out, detail);
    dt_bilateral_free(b);
  }

  // some aliased pointers for compilers that don't yet understand operators on __m128
  const float *const Labminf = (float *)&Labmin;
  const float *const Labmaxf = (float *)&Labmax;
#ifdef _OPENMP
  #pragma omp parallel for default(none) shared(in,out,data,roi_out) schedule(static)
#endif
  for(int k=0; k<roi_out->width*roi_out->height; k++)
  {
    out[k*ch+0] = (out[k*ch+0] < 100.0f) ? data->table[CLAMP((int)(out[k*ch+0]/100.0f*0x10000ul), 0, 0xffff)] :
                  dt_iop_eval_exp(data->unbounded_coeffs, out[k*ch+0]/100.0f);
    out[k*ch+1] = CLAMPF(out[k*ch+1]*data->saturation, Labminf[1], Labmaxf[1]);
    out[k*ch+2] = CLAMPF(out[k*ch+2]*data->saturation, Labminf[2], Labmaxf[2]);
    out[k*ch+3] = in[k*ch+3];
  }
}


static void
radius_callback (GtkWidget *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_lowpass_params_t *p = (dt_iop_lowpass_params_t *)self->params;
  p->radius = copysignf(dt_bauhaus_slider_get(slider), p->radius);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void
bilat_callback (GtkWidget *widget, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_lowpass_params_t *p = (dt_iop_lowpass_params_t *)self->params;
  if(dt_bauhaus_combobox_get(widget))
    p->radius = -fabsf(p->radius);
  else
    p->radius = fabsf(p->radius);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void
contrast_callback (GtkWidget *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_lowpass_params_t *p = (dt_iop_lowpass_params_t *)self->params;
  p->contrast = dt_bauhaus_slider_get(slider);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void
saturation_callback (GtkWidget *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_lowpass_params_t *p = (dt_iop_lowpass_params_t *)self->params;
  p->saturation = dt_bauhaus_slider_get(slider);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

#if 0 // gaussian order not user selectable
static void
order_changed (GtkComboBox *combo, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_lowpass_params_t *p = (dt_iop_lowpass_params_t *)self->params;
  p->order = gtk_combo_box_get_active(combo);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}
#endif

void
commit_params (struct dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_lowpass_params_t *p = (dt_iop_lowpass_params_t *)p1;
#ifdef HAVE_GEGL
  fprintf(stderr, "[lowpass] TODO: implement gegl version!\n");
  // pull in new params to gegl
#else
  dt_iop_lowpass_data_t *d = (dt_iop_lowpass_data_t *)piece->data;
  d->order = p->order;
  d->radius = p->radius;
  d->contrast = p->contrast;
  d->saturation = p->saturation;

#ifdef HAVE_OPENCL
  if(d->radius < 0.0f)
    piece->process_cl_ready = (piece->process_cl_ready && !(darktable.opencl->avoid_atomics));
#endif

  if(fabs(d->contrast) <= 1.0f)
  {
    // linear curve for contrast up to +/- 1
    for(int k=0; k<0x10000; k++) d->table[k] = d->contrast*(100.0f*k/0x10000 - 50.0f) + 50.0f;
  }
  else
  {
    // sigmoidal curve for contrast above +/-1 1
    // going from (0,0) to (1,100) or (0,100) to (1,0), respectively
    const float boost = 5.0f;
    const float contrastm1sq = boost*(fabs(d->contrast) - 1.0f)*(fabs(d->contrast) - 1.0f);
    const float contrastscale = copysign(sqrt(1.0f + contrastm1sq), d->contrast);
#ifdef _OPENMP
    #pragma omp parallel for default(none) shared(d) schedule(static)
#endif
    for(int k=0; k<0x10000; k++)
    {
      float kx2m1 = 2.0f*(float)k/0x10000 - 1.0f;
      d->table[k] = 50.0f * (contrastscale * kx2m1 / sqrtf(1.0f + contrastm1sq * kx2m1 * kx2m1) + 1.0f);
    }
  }

  // now the extrapolation stuff:
  const float x[4] = {0.7f, 0.8f, 0.9f, 1.0f};
  const float y[4] = {d->table[CLAMP((int)(x[0]*0x10000ul), 0, 0xffff)],
                      d->table[CLAMP((int)(x[1]*0x10000ul), 0, 0xffff)],
                      d->table[CLAMP((int)(x[2]*0x10000ul), 0, 0xffff)],
                      d->table[CLAMP((int)(x[3]*0x10000ul), 0, 0xffff)]
                     };
  dt_iop_estimate_exp(x, y, 4, d->unbounded_coeffs);
#endif
}

void init_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
#ifdef HAVE_GEGL
  // create part of the gegl pipeline
  piece->data = NULL;
#else
  dt_iop_lowpass_data_t *d = (dt_iop_lowpass_data_t *)malloc(sizeof(dt_iop_lowpass_data_t));
  piece->data =  (void *)d;
  memset(piece->data,0,sizeof(dt_iop_lowpass_data_t));
  self->commit_params(self, self->default_params, pipe, piece);
  for(int k=0; k<0x10000; k++) d->table[k] = 100.0f*k/0x10000; // identity
#endif
}

void cleanup_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
#ifdef HAVE_GEGL
  // clean up everything again.
  (void)gegl_node_remove_child(pipe->gegl, piece->input);
  // no free necessary, no data is alloc'ed
#else
  free(piece->data);
#endif
}

void gui_update(struct dt_iop_module_t *self)
{
  dt_iop_module_t *module = (dt_iop_module_t *)self;
  dt_iop_lowpass_gui_data_t *g = (dt_iop_lowpass_gui_data_t *)self->gui_data;
  dt_iop_lowpass_params_t *p = (dt_iop_lowpass_params_t *)module->params;
  dt_bauhaus_slider_set(g->scale1, fabsf(p->radius));
  dt_bauhaus_combobox_set(g->bilat, p->radius < 0 ? 1 : 0);
  dt_bauhaus_slider_set(g->scale2, p->contrast);
  dt_bauhaus_slider_set(g->scale3, p->saturation);
  //gtk_combo_box_set_active(g->order, p->order);
}

void init(dt_iop_module_t *module)
{
  module->params = malloc(sizeof(dt_iop_lowpass_params_t));
  module->default_params = malloc(sizeof(dt_iop_lowpass_params_t));
  module->default_enabled = 0;
  module->priority = 736; // module order created by iop_dependencies.py, do not edit!
  module->params_size = sizeof(dt_iop_lowpass_params_t);
  module->gui_data = NULL;
  dt_iop_lowpass_params_t tmp = (dt_iop_lowpass_params_t)
  {
    0, 10, 1, 1
  };
  memcpy(module->params, &tmp, sizeof(dt_iop_lowpass_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_lowpass_params_t));
}

void init_global(dt_iop_module_so_t *module)
{
  const int program = 6; // gaussian.cl, from programs.conf
  dt_iop_lowpass_global_data_t *gd = (dt_iop_lowpass_global_data_t *)malloc(sizeof(dt_iop_lowpass_global_data_t));
  module->data = gd;
  gd->kernel_lowpass_mix = dt_opencl_create_kernel(program, "lowpass_mix");
}

void init_presets (dt_iop_module_so_t *self)
{
  DT_DEBUG_SQLITE3_EXEC(dt_database_get(darktable.db), "begin", NULL, NULL, NULL);

  dt_gui_presets_add_generic(_("local contrast mask"), self->op, self->version(), &(dt_iop_lowpass_params_t)
  {
    0, 50.0f, -1.0f, 0.0f
  }, sizeof(dt_iop_lowpass_params_t), 1);

  DT_DEBUG_SQLITE3_EXEC(dt_database_get(darktable.db), "commit", NULL, NULL, NULL);
}

void cleanup(dt_iop_module_t *module)
{
  free(module->gui_data);
  module->gui_data = NULL;
  free(module->params);
  module->params = NULL;
}

void cleanup_global(dt_iop_module_so_t *module)
{
  dt_iop_lowpass_global_data_t *gd = (dt_iop_lowpass_global_data_t *)module->data;
  dt_opencl_free_kernel(gd->kernel_lowpass_mix);
  free(module->data);
  module->data = NULL;
}


void gui_init(struct dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_lowpass_gui_data_t));
  dt_iop_lowpass_gui_data_t *g = (dt_iop_lowpass_gui_data_t *)self->gui_data;
  dt_iop_lowpass_params_t *p = (dt_iop_lowpass_params_t *)self->params;

  self->widget = gtk_vbox_new(TRUE, DT_BAUHAUS_SPACE);

#if 0 // gaussian is order not user selectable here, as it does not make much sense for a lowpass filter
  GtkBox *hbox  = GTK_BOX(gtk_hbox_new(FALSE, 5));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(hbox), FALSE, FALSE, 0);
  GtkWidget *label = dtgtk_reset_label_new(_("filter order"), self, &p->order, sizeof(float));
  gtk_box_pack_start(hbox, label, FALSE, FALSE, 0);
  g->order = GTK_COMBO_BOX(gtk_combo_box_new_text());
  gtk_combo_box_append_text(g->order, _("0th order"));
  gtk_combo_box_append_text(g->order, _("1st order"));
  gtk_combo_box_append_text(g->order, _("2nd order"));
  gtk_object_set(GTK_OBJECT(g->order), "tooltip-text", _("filter order of gaussian blur"), (char *)NULL);
  gtk_box_pack_start(hbox, GTK_WIDGET(g->order), TRUE, TRUE, 0);
#endif

  g->scale1 = dt_bauhaus_slider_new_with_range(self,0.1, 200.0, 0.1, p->radius, 2);
  g->scale2 = dt_bauhaus_slider_new_with_range(self,-3.0, 3.0, 0.01, p->contrast, 2);
  g->scale3 = dt_bauhaus_slider_new_with_range(self,-3.0, 3.0, 0.01, p->saturation, 2);

  dt_bauhaus_widget_set_label(g->scale1,_("radius"));
  dt_bauhaus_widget_set_label(g->scale2,_("contrast"));
  dt_bauhaus_widget_set_label(g->scale3,_("saturation"));

  g->bilat  = dt_bauhaus_combobox_new(self);
  dt_bauhaus_widget_set_label(g->bilat, _("soften with"));
  dt_bauhaus_combobox_add(g->bilat, _("gaussian"));
  dt_bauhaus_combobox_add(g->bilat, _("bilateral filter"));

  gtk_box_pack_start(GTK_BOX(self->widget), g->scale1, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), g->bilat,  TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), g->scale2, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), g->scale3, TRUE, TRUE, 0);
  gtk_object_set(GTK_OBJECT(g->scale1), "tooltip-text", _("radius of gaussian/bilateral blur"), (char *)NULL);
  gtk_object_set(GTK_OBJECT(g->scale2), "tooltip-text", _("contrast of lowpass filter"), (char *)NULL);
  gtk_object_set(GTK_OBJECT(g->scale3), "tooltip-text", _("color saturation of lowpass filter"), (char *)NULL);
  gtk_object_set(GTK_OBJECT(g->bilat),  "tooltip-text", _("which filter to use for blurring"), (char *)NULL);

  g_signal_connect (G_OBJECT (g->scale1), "value-changed",
                    G_CALLBACK (radius_callback), self);
  g_signal_connect (G_OBJECT (g->bilat), "value-changed",
                    G_CALLBACK (bilat_callback), self);
  g_signal_connect (G_OBJECT (g->scale2), "value-changed",
                    G_CALLBACK (contrast_callback), self);
  g_signal_connect (G_OBJECT (g->scale3), "value-changed",
                    G_CALLBACK (saturation_callback), self);
#if 0 // gaussian order not user selectable
  g_signal_connect (G_OBJECT (g->order), "changed",
                    G_CALLBACK (order_changed), self);
#endif
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  free(self->gui_data);
  self->gui_data = NULL;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
