/*
    This file is part of darktable,
    copyright (c) 2011 johannes hanika.

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
#include "develop/imageop.h"
#include "develop/masks.h"
#include "control/control.h"
#include "control/conf.h"
#include "gui/gtk.h"
#include <gtk/gtk.h>
#include <stdlib.h>


#define EPSILON -0.00001

// this is the version of the modules parameters,
// and includes version information about compile-time dt
DT_MODULE(2)

typedef struct spot_t
{
  double formid;
  float source[2];
  
  float opacity;  //between 0 and 1
  
  int version; //old version = 1 ; new version = 2
}
spot_t;

typedef struct dt_iop_spots_params_t
{
  int num_spots;
  spot_t spot[32];
  
  //just to say "hey, there has some some change to the params" and avoid cache problem
  int change;
}
dt_iop_spots_params_t;

typedef struct spot_draw_t
{
  //for both source and spot, first point is the center
  // points to draw the source
  float *source;
  float *source_border;
  int source_count;
  int source_border_count;
  // points to draw the spot
  float *spot;
  int spot_count;
  //are the buffers allocated ? 
  int ok;
}
spot_draw_t;

typedef struct dt_iop_spots_gui_data_t
{
  GtkLabel *label;
  int dragging;
  int selected;
  gboolean hoover_c; // is the pointer over the "clone from" end?
  gboolean border;  //is the pointer in the "border" part ?
  float last_radius;
  spot_draw_t spot[32];
  dt_masks_point_circle_t* form[32];
  uint64_t pipe_hash;
}
dt_iop_spots_gui_data_t;

typedef struct dt_iop_spots_params_t dt_iop_spots_data_t;

// this returns a translatable name
const char *name()
{
  return _("spot removal");
}

int
groups ()
{
  return IOP_GROUP_CORRECT;
}

int legacy_params (dt_iop_module_t *self, const void *const old_params, const int old_version, void *new_params, const int new_version)
{
  /*if(old_version == 1 && new_version == 2)
  {
    typedef struct dt_iop_spots_v1_t
    {
      float x,y;
      float xc,yc;
      float radius;
    }
    dt_iop_spots_v1_t;
    typedef struct dt_iop_spots_params_v1_t
    {
      int num_spots;
      dt_iop_spots_v1_t spot[32];
    }
    dt_iop_spots_params_v1_t;

    dt_iop_spots_params_v1_t *o = (dt_iop_spots_params_v1_t *)old_params;
    dt_iop_spots_params_t *n = (dt_iop_spots_params_t *)new_params;
    dt_iop_spots_params_t *d = (dt_iop_spots_params_t *)self->default_params;

    *n = *d;  // start with a fresh copy of default parameters
    n->num_spots = o->num_spots;
    for (int i=0; i<n->num_spots; i++)
    {
      n->spot[i].version = 1;
      n->spot[i].opacity = 1.0f;
      n->spot[i].spot.center[0] = o->spot[i].x;
      n->spot[i].spot.center[1] = o->spot[i].y;
      n->spot[i].source[0] = o->spot[i].xc;
      n->spot[i].source[1] = o->spot[i].yc;
      n->spot[i].spot.border = 0.0f;
      n->spot[i].spot.radius = o->spot[i].radius;
    }
    return 0;
  }*/
  return 1;
}

static void gui_spot_add(dt_iop_module_t *self, spot_draw_t *gspt, int spot_index)
{
  dt_develop_t *dev = self->dev;
  dt_iop_spots_params_t   *p = (dt_iop_spots_params_t   *)self->params;
  gspt->ok = 0;
  gspt->source_count = gspt->source_border_count = gspt->spot_count = 0;
  dt_masks_form_t *form = dt_masks_get_from_id(self->dev,p->spot[spot_index].formid);
  dt_masks_point_circle_t *circle = dt_masks_get_circle(form);
  
  float dx = circle->center[0] - p->spot[spot_index].source[0];
  float dy = circle->center[1] - p->spot[spot_index].source[1];
  if (dt_masks_get_points(dev,form, &gspt->source, &gspt->source_count,dx,dy))
  {
    if (dt_masks_get_points(dev,form, &gspt->spot, &gspt->spot_count,0,0))
    {
      if (p->spot[spot_index].version > 1)
      {
        if (dt_masks_get_border(dev,form, &gspt->source_border, &gspt->source_border_count,dx,dy))
        {
          gspt->ok = 1;
        }
      }
      else gspt->ok = 1;
    }
  }
}
static void gui_spot_remove(dt_iop_module_t *self, spot_draw_t *gspt, int spot_index)
{
  dt_iop_spots_params_t   *p = (dt_iop_spots_params_t   *)self->params;
  
  gspt->source_count = gspt->source_border_count = gspt->spot_count = 0;
  free(gspt->source);
  gspt->source = NULL;
  if (p->spot[spot_index].version > 1)
  {
    free(gspt->source_border);
    gspt->source_border = NULL;
  }
  free(gspt->spot);
  gspt->spot = NULL;
  gspt->ok = 0;
}

static void gui_spot_update_source(dt_iop_module_t *self, spot_draw_t *gspt, int spot_index)
{
  dt_develop_t *dev = self->dev;
  dt_iop_spots_params_t   *p = (dt_iop_spots_params_t   *)self->params;
  dt_masks_form_t *form = dt_masks_get_from_id(self->dev,p->spot[spot_index].formid);
  dt_masks_point_circle_t *circle = dt_masks_get_circle(form);
  //we remove the source buffers
  gspt->source_count = gspt->source_border_count = 0;
  free(gspt->source);
  gspt->source = NULL;
  if (p->spot[spot_index].version > 1)
  {
    free(gspt->source_border);
    gspt->source_border = NULL;
  }
  
  //and we recreate them
  float dx = circle->center[0] - p->spot[spot_index].source[0];
  float dy = circle->center[1] - p->spot[spot_index].source[1];
  if (dt_masks_get_points(dev,form, &gspt->source, &gspt->source_count,dx,dy))
  {
    if (p->spot[spot_index].version > 1)
    {
      if (dt_masks_get_border(dev,form, &gspt->source_border, &gspt->source_border_count,dx,dy))
      {
        return;
      }
    }
    else return;
  }
    
      
  //if it fails, we delete spot buffer too
  gspt->spot_count = 0;
  free(gspt->spot);
  gspt->spot = NULL;
  gspt->ok = 0;
}
static void gui_spot_update_spot(dt_iop_module_t *self, spot_draw_t *gspt, int spot_index)
{
  //we remove the spot buffer
  gspt->spot_count = 0;
  free(gspt->spot);
  gspt->spot = NULL;
  
  //and we recreate it
  dt_develop_t *dev = self->dev;
  dt_iop_spots_params_t   *p = (dt_iop_spots_params_t   *)self->params;
  dt_masks_form_t *form = dt_masks_get_from_id(self->dev,p->spot[spot_index].formid);
  
  if (dt_masks_get_points(dev,form, &gspt->spot, &gspt->spot_count,0,0))
  {
    return;
  }
  
  //if it fails, we delete source buffers too
  gspt->source_count = gspt->source_border_count = 0;
  free(gspt->source);
  gspt->source = NULL;
  if (p->spot[spot_index].version > 1)
  {
    free(gspt->source_border);
    gspt->source_border = NULL;
  }
  gspt->ok = 0;
}
static void gui_spot_update_radius(dt_iop_module_t *self, spot_draw_t *gspt, int spot_index)
{
  //we remove and re-add the point
  gui_spot_remove(self,gspt,spot_index);
  gui_spot_add(self,gspt,spot_index);
}

static int gui_spot_test_create(dt_iop_module_t *self)
{
  dt_iop_spots_params_t   *p = (dt_iop_spots_params_t   *)self->params;
  dt_iop_spots_gui_data_t   *g = (dt_iop_spots_gui_data_t   *)self->gui_data;
  
  //we test if the image has changed
  if (g->pipe_hash >= 0)
  {
    if (g->pipe_hash != self->dev->preview_pipe->backbuf_hash)
    {
      for (int i=0; i<32; i++)
        if (g->spot[i].ok) gui_spot_remove(self,&g->spot[i],i);
      g->pipe_hash = 0;
    }
  }
  
  //we create the spots if needed
  for(int i=0; i<p->num_spots; i++)
  {
    if (!g->spot[i].ok)
    {
      gui_spot_add(self,&g->spot[i],i);
      if (!g->spot[i].ok) return 0;
    }
  }
  g->pipe_hash = self->dev->preview_pipe->backbuf_hash;
  return 1;
}

void modify_roi_out(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, dt_iop_roi_t *roi_out, const dt_iop_roi_t *roi_in)
{
  *roi_out = *roi_in;
}

void modify_roi_in(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, const dt_iop_roi_t *roi_out, dt_iop_roi_t *roi_in)
{
  *roi_in = *roi_out;
  
  int roir = roi_in->width+roi_in->x;
  int roib = roi_in->height+roi_in->y;
  int roix = roi_in->x;
  int roiy = roi_in->y;
  
  dt_iop_spots_params_t *d = (dt_iop_spots_params_t *)piece->data;
  
  //We calcul full image width and height at scale of ROI
  const int imw = CLAMP(piece->pipe->iwidth*roi_in->scale, 1, piece->pipe->iwidth);
  const int imh = CLAMP(piece->pipe->iheight*roi_in->scale, 1, piece->pipe->iheight);
  
  // We iterate throught all spots or polygons
  for(int i=0; i<d->num_spots; i++)
  {
    //we get the spot
    dt_masks_form_t *form = dt_masks_get_from_id(self->dev,d->spot[i].formid);
    if (!form) continue;
    dt_masks_point_circle_t *circle = dt_masks_get_circle(form);
    if (!circle) continue;
    // convert in full image space (at scale of ROI)
    const int x = circle->center[0] *imw, y = circle->center[1] *imh;
    const int xc = d->spot[i].source[0]*imw, yc = d->spot[i].source[1]*imh;
    //const int rad = d->spot[i].spot.radius * MIN(imw, imh);
    
    int w,h,l,t;
    dt_masks_get_area(self,piece->pipe,imw,imh,form,&w,&h,&l,&t);
    
    //If the destination is outside the ROI, we skip this form !
    if (t>=roi_out->y+roi_out->height || t+h<=roi_out->y || l>=roi_out->x+roi_out->width || l+w<=roi_out->x) continue;
    
    //we only process the visible part of the destination
    if (t<=roi_out->y) h -= roi_out->y-t, t = roi_out->y;
    if (t+h>=roi_out->height+roi_out->y) h = roi_out->y+roi_out->height-1-t;
    if (l<=roi_out->x) w -= roi_out->x-l, l = roi_out->x;
    if (l+w>=roi_out->width+roi_out->x) w = roi_out->x+roi_out->width-1-l;
    
    //we don't process part of the source outside the image
    roiy = fminf(t+yc-y,roiy);
    roix = fminf(l+xc-x,roix);
    roir = fmaxf(l+w+xc-x,roir);
    roib = fmaxf(t+h+yc-y,roib);
  }
  
  //now we set the values
  roi_in->x = CLAMP(roix, 0, piece->pipe->iwidth-1);
  roi_in->y = CLAMP(roiy, 0, piece->pipe->iheight-1);
  roi_in->width = CLAMP(roir-roi_in->x, 1, piece->pipe->iwidth-roi_in->x);
  roi_in->height = CLAMP(roib-roi_in->y, 1, piece->pipe->iheight-roi_in->y);
}

void process (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, void *i, void *o, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{
  dt_iop_spots_params_t *d = (dt_iop_spots_params_t *)piece->data;
  // const float scale = piece->iscale/roi_in->scale;
  const float scale = 1.0f/roi_in->scale;
  
  const int ch = piece->colors;
  const float *in = (float *)i;
  float *out = (float *)o;
  
  //We calcul full image width and height at scale of ROI
  const int imw = CLAMP(piece->pipe->iwidth*roi_in->scale, 1, piece->pipe->iwidth);
  const int imh = CLAMP(piece->pipe->iheight*roi_in->scale, 1, piece->pipe->iheight);
  
  // we don't modify most of the image:
#ifdef _OPENMP
    #pragma omp parallel for schedule(static) default(none) shared(d,out,in,roi_in,roi_out)
#endif
  for (int k=0; k<roi_out->height; k++)
  {
    float *outb = out + ch*k*roi_out->width;
    const float *inb =  in + ch*roi_in->width*(k+roi_out->y-roi_in->y) + ch*(roi_out->x-roi_in->x);
    memcpy(outb, inb, sizeof(float)*roi_out->width*ch);
  }
    
  // iterate throught all forms
  for(int i=0; i<d->num_spots; i++)
  {
    //we get the spot
    dt_masks_form_t *form = dt_masks_get_from_id(self->dev,d->spot[i].formid);
    if (!form) continue;
    dt_masks_point_circle_t *circle = dt_masks_get_circle(form);
    if (!circle) continue;
    
    //we first need to know if we copy the entire form or not
    // convert in full image space (at scale of ROI)
    const int x = circle->center[0] *imw, y = circle->center[1] *imh;
    const int xc = d->spot[i].source[0]*imw, yc = d->spot[i].source[1]*imh;
    
    int w,h,l,t;
    dt_masks_get_area(self,piece->pipe,imw,imh,form,&w,&h,&l,&t);
    
    //If the destination is outside the ROI, we skip this form !
    if (t>=roi_out->y+roi_out->height || t+h<=roi_out->y || l>=roi_out->x+roi_out->width || l+w<=roi_out->x) continue;
    
    //we only process the visible part of the destination
    if (t<=roi_out->y) h -= roi_out->y+1-t, t = roi_out->y+1;
    if (t+h>=roi_out->height+roi_out->y) h = roi_out->y+roi_out->height-1-t;
    if (l<=roi_out->x) w -= roi_out->x+1-l, l = roi_out->x+1;
    if (l+w>=roi_out->width+roi_out->x) w = roi_out->x+roi_out->width-1-l;
    
    //we don't process part of the source outside the roi_in
    if (t+yc-y<=roi_in->y) h -= roi_in->y-yc+y+1-t ,t = roi_in->y-yc+y+1;      
    if (t+h+yc-y>=roi_in->y+roi_in->height) h = roi_in->y+roi_in->height-yc+y-t-1;      
    if (l+xc-x<=roi_in->x) w -= roi_in->x-xc+x+1-l ,l = roi_in->x-xc+x+1;
    if (l+w+xc-x>=roi_in->width+roi_in->x) w = roi_in->x+roi_in->width-xc+x-l-1;
    
    if (d->spot[i].version > 1)
    {
      //we get the mask
      float *mask;
      int posx,posy,width,height;    
      dt_masks_get_mask(self,piece->pipe,roi_in->scale*piece->buf_in.width,roi_in->scale*piece->buf_in.height,form,&mask,&width,&height,&posx,&posy);
      int posx_source = posx + (d->spot[i].source[0] - circle->center[0])*roi_in->scale*piece->buf_in.width;
      int posy_source = posy + (d->spot[i].source[1] - circle->center[1])*roi_in->scale*piece->buf_in.height;
  
      for (int yy=t ; yy<t+h; yy++)
        for (int xx=l ; xx<l+w; xx++)
        {
          float f = mask[(yy-posy)*width + xx - posx]*d->spot[i].opacity;
          
          for(int c=0; c<ch; c++)
            out[4*(roi_out->width*(yy-roi_out->y) + xx-roi_out->x) + c] =
              out[4*(roi_out->width*(yy-roi_out->y) + xx-roi_out->x) + c] * (1.0f-f) +
              in[4*(roi_in->width*(yy-posy+posy_source-roi_in->y) + xx-posx+posx_source-roi_in->x) + c] * f;
        }
    }
    else
    {
      // convert from world space:
      const int posx  = (circle->center[0] *piece->buf_in.width)/scale;
      const int posy  = (circle->center[1] *piece->buf_in.height)/scale;
      const int posx_source = (d->spot[i].source[0]*piece->buf_in.width)/scale;
      const int posy_source = (d->spot[i].source[1]*piece->buf_in.height)/scale;      
      const int rad = circle->radius* MIN(piece->buf_in.width, piece->buf_in.height)/scale;
      //const int um = -MIN(rad, MIN(x, xc));
      //const int uM = MIN(rad, MIN(roi_in->width-1-xc, roi_in->width-1-x));
      //const int vm = -MIN(rad, MIN(y, yc));
      //const int vM = MIN(rad, MIN(roi_in->height-1-yc, roi_in->height-1-y));
    w = h = 2*rad;
    
    //If the destination is outside the ROI, we skip this form !
    if (t>=roi_out->y+roi_out->height || t+h<=roi_out->y || l>=roi_out->x+roi_out->width || l+w<=roi_out->x) continue;
    
    //we only process the visible part of the destination
    if (t<=roi_out->y) h -= roi_out->y+1-t, t = roi_out->y+1;
    if (t+h>=roi_out->height+roi_out->y) h = roi_out->y+roi_out->height-1-t;
    if (l<=roi_out->x) w -= roi_out->x+1-l, l = roi_out->x+1;
    if (l+w>=roi_out->width+roi_out->x) w = roi_out->x+roi_out->width-1-l;
    
    //we don't process part of the source outside the roi_in
    if (t+yc-y<=roi_in->y) h -= roi_in->y-yc+y+1-t ,t = roi_in->y-yc+y+1;      
    if (t+h+yc-y>=roi_in->y+roi_in->height) h = roi_in->y+roi_in->height-yc+y-t-1;      
    if (l+xc-x<=roi_in->x) w -= roi_in->x-xc+x+1-l ,l = roi_in->x-xc+x+1;
    if (l+w+xc-x>=roi_in->width+roi_in->x) w = roi_in->x+roi_in->width-xc+x-l-1;
    
    // convert from world space:     
      float filter[2*rad + 1];
      // for(int k=-rad; k<=rad; k++) filter[rad + k] = expf(-k*k*2.f/(rad*rad));
      if(rad > 0)
      {
        for(int k=-rad; k<=rad; k++)
        {
          const float kk = 1.0f - fabsf(k/(float)rad);
          filter[rad + k] = kk*kk*(3.0f - 2.0f*kk);
        }
      }
      else
      {
        filter[0] = 1.0f;
      }
      for (int yy=t ; yy<t+h; yy++)
        for (int xx=l ; xx<l+w; xx++)
        {
          const float f = filter[xx-posx+rad+1]*filter[yy-posy+rad+1]*d->spot[i].opacity;          
          for(int c=0; c<ch; c++)
            out[4*(roi_out->width*(yy-roi_out->y) + xx-roi_out->x) + c] =
              out[4*(roi_out->width*(yy-roi_out->y) + xx-roi_out->x) + c] * (1.0f-f) +
              in[4*(roi_in->width*(yy-posy+posy_source-roi_in->y) + xx-posx+posx_source-roi_in->x) + c] * f;
        }
    }
    
  }
}

/** init, cleanup, commit to pipeline */
void init(dt_iop_module_t *module)
{
  // we don't need global data:
  module->data = NULL; //malloc(sizeof(dt_iop_spots_global_data_t));
  module->params = malloc(sizeof(dt_iop_spots_params_t));
  module->default_params = malloc(sizeof(dt_iop_spots_params_t));
  // our module is disabled by default
  // by default:
  module->default_enabled = 0;
  module->priority = 200; // module order created by iop_dependencies.py, do not edit!
  module->params_size = sizeof(dt_iop_spots_params_t);
  module->gui_data = NULL;
  // init defaults:
  dt_iop_spots_params_t tmp = (dt_iop_spots_params_t)
  {
    0
  };

  memcpy(module->params, &tmp, sizeof(dt_iop_spots_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_spots_params_t));
}

void cleanup(dt_iop_module_t *module)
{
  free(module->gui_data);
  module->gui_data = NULL; // just to be sure
  free(module->params);
  module->params = NULL;
  free(module->data); // just to be sure
  module->data = NULL;
}

void gui_focus (struct dt_iop_module_t *self, gboolean in)
{
  dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  if(self->enabled)
  {
    if(in)
    {
      // got focus.
      gui_spot_test_create(self);
    }
    else
    {
      // lost focus, delete all gui drawing
      for (int i=0; i<32; i++)
      {
        if (g->spot[i].ok) gui_spot_remove(self,&g->spot[i],i);
      }
    }
  }
}

/** commit is the synch point between core and gui, so it copies params to pipe data. */
void commit_params (struct dt_iop_module_t *self, dt_iop_params_t *params, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  memcpy(piece->data, params, sizeof(dt_iop_spots_params_t));
}

void init_pipe     (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  piece->data = malloc(sizeof(dt_iop_spots_data_t));
  self->commit_params(self, self->default_params, pipe, piece);
}

void cleanup_pipe  (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  free(piece->data);
}

/** gui callbacks, these are needed. */
void gui_update    (dt_iop_module_t *self)
{
  dt_iop_spots_params_t *p = (dt_iop_spots_params_t *)self->params;
  dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  char str[3];
  snprintf(str,3,"%d",p->num_spots);
  gtk_label_set_text(g->label, str);

  for (int i=0; i<p->num_spots; i++)
  {
    //we get the spot
    dt_masks_form_t *form = dt_masks_get_from_id(self->dev,p->spot[i].formid);
    if (!form) continue;
    g->form[i] = dt_masks_get_circle(form);
  }
}

void gui_init     (dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_spots_gui_data_t));
  dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  g->dragging = -1;
  g->selected = -1;
  g->border = FALSE;
  g->last_radius = MAX(0.01f, dt_conf_get_float("ui_last/spot_size"));

  for (int i=0; i<32; i++)
  {
    g->spot[i].ok = 0;
  }
  g->pipe_hash = 0;
  
  self->widget = gtk_vbox_new(FALSE, 5);
  GtkWidget *label = gtk_label_new(_("click on a spot and drag on canvas to heal.\nuse the mouse wheel to adjust size.\nright click to remove a stroke."));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0f, 0.5f);
  gtk_box_pack_start(GTK_BOX(self->widget), label, FALSE, TRUE, 0);
  GtkWidget * hbox = gtk_hbox_new(FALSE, 5);
  label = gtk_label_new(_("number of strokes:"));
  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 0);
  g->label = GTK_LABEL(gtk_label_new("-1"));
  gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(g->label), FALSE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), hbox, TRUE, TRUE, 0);
}

void gui_cleanup  (dt_iop_module_t *self)
{
  dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  // nothing else necessary, gtk will clean up the labels
  for (int i=0; i<32; i++)
  {
    if (g->spot[i].ok) gui_spot_remove(self,&g->spot[i],i);
  }
  free(self->gui_data);
  self->gui_data = NULL;
}

void gui_post_expose(dt_iop_module_t *self, cairo_t *cr, int32_t width, int32_t height, int32_t pointerx, int32_t pointery)
{
  dt_develop_t *dev = self->dev;
  dt_iop_spots_params_t   *p = (dt_iop_spots_params_t   *)self->params;
  dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  float wd = dev->preview_pipe->backbuf_width;
  float ht = dev->preview_pipe->backbuf_height;
  if (wd < 1.0 || ht < 1.0) return;
  float pzx, pzy;
  dt_dev_get_pointer_zoom_pos(dev, pointerx, pointery, &pzx, &pzy);
  pzx += 0.5f;
  pzy += 0.5f;
  float zoom_x, zoom_y;
  int32_t zoom, closeup;
  DT_CTL_GET_GLOBAL(zoom_y, dev_zoom_y);
  DT_CTL_GET_GLOBAL(zoom_x, dev_zoom_x);
  DT_CTL_GET_GLOBAL(zoom, dev_zoom);
  DT_CTL_GET_GLOBAL(closeup, dev_closeup);
  float zoom_scale = dt_dev_get_zoom_scale(dev, zoom, closeup ? 2 : 1, 1);
  
  cairo_set_source_rgb(cr, .3, .3, .3);

  cairo_translate(cr, width/2.0, height/2.0f);
  cairo_scale(cr, zoom_scale, zoom_scale);
  cairo_translate(cr, -.5f*wd-zoom_x*wd, -.5f*ht-zoom_y*ht);

  double dashed[] = {4.0, 4.0};
  dashed[0] /= zoom_scale;
  dashed[1] /= zoom_scale;
  int len  = sizeof(dashed) / sizeof(dashed[0]);
  
  cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
  
  //we update the spots if needed
  if (!gui_spot_test_create(self)) return;
  
  for(int i=0; i<p->num_spots; i++)
  {
    spot_draw_t gspt = g->spot[i];    
    float src_x=0, src_y=0, spt_x=0, spt_y=0;
    
    //source
    if (gspt.source_count > 6)
    { 
      cairo_set_dash(cr, dashed, 0, 0);     
      if(i == g->selected || i == g->dragging) cairo_set_line_width(cr, 5.0/zoom_scale);
      else                                     cairo_set_line_width(cr, 3.0/zoom_scale);
      cairo_set_source_rgba(cr, .3, .3, .3, .8);
      if (g->dragging == i && g->hoover_c)
      {
        src_x = p->spot[i].source[0]*wd;
        src_y = p->spot[i].source[1]*ht;
        float dx = src_x - gspt.source[0], dy = src_y - gspt.source[1];
        cairo_move_to(cr,gspt.source[2]+dx,gspt.source[3]+dy);
        for (int i=2; i<gspt.source_count; i++)
        {
          cairo_line_to(cr,gspt.source[i*2]+dx,gspt.source[i*2+1]+dy);
        }
        cairo_line_to(cr,gspt.source[2]+dx,gspt.source[3]+dy);
      }
      else
      {
        cairo_move_to(cr,gspt.source[2],gspt.source[3]);
        for (int i=2; i<gspt.source_count; i++)
        {
          cairo_line_to(cr,gspt.source[i*2],gspt.source[i*2+1]);
        }
        cairo_line_to(cr,gspt.source[2],gspt.source[3]);
        src_x = gspt.source[0];
        src_y = gspt.source[1];
      }
      cairo_stroke_preserve(cr);
      if(i == g->selected || i == g->dragging) cairo_set_line_width(cr, 2.0/zoom_scale);
      else                                     cairo_set_line_width(cr, 1.0/zoom_scale);
      cairo_set_source_rgba(cr, .8, .8, .8, .8);
      cairo_stroke(cr);
    }

    //border
    if (gspt.source_border_count > 6)
    { 
      cairo_set_dash(cr, dashed, len, 0);     
      if(i == g->selected || i == g->dragging) cairo_set_line_width(cr, 2.0/zoom_scale);
      else                                     cairo_set_line_width(cr, 1.0/zoom_scale);
      cairo_set_source_rgba(cr, .3, .3, .3, .8);
      if (g->dragging == i && g->hoover_c)
      {
        //nothing to do...
      }
      else
      {
        cairo_move_to(cr,gspt.source_border[2],gspt.source_border[3]);
        for (int i=2; i<gspt.source_border_count; i++)
        {
          cairo_line_to(cr,gspt.source_border[i*2],gspt.source_border[i*2+1]);
        }
        cairo_line_to(cr,gspt.source_border[2],gspt.source_border[3]);
      }
      cairo_stroke_preserve(cr);
      if(i == g->selected || i == g->dragging) cairo_set_line_width(cr, 2.0/zoom_scale);
      else                                     cairo_set_line_width(cr, 1.0/zoom_scale);
      cairo_set_source_rgba(cr, .8, .8, .8, .8);
      cairo_set_dash(cr, dashed, len, 4);
      cairo_stroke(cr);
    }
    
    //spot
    if (gspt.spot_count > 6)
    {
      cairo_set_dash(cr, dashed, 0,0);
      if(i == g->selected || i == g->dragging) cairo_set_line_width(cr, 5.0/zoom_scale);
      else                                     cairo_set_line_width(cr, 3.0/zoom_scale);
      cairo_set_source_rgba(cr, .3, .3, .3, .8);
      if (g->dragging == i && !g->hoover_c)
      {
        spt_x = g->form[i]->center[0]*wd;
        spt_y = g->form[i]->center[1]*ht;
        float dx = spt_x - gspt.spot[0], dy = spt_y - gspt.spot[1];
        cairo_move_to(cr,gspt.spot[2]+dx,gspt.spot[3]+dy);
        for (int i=2; i<gspt.spot_count; i++)
        {
          cairo_line_to(cr,gspt.spot[i*2]+dx,gspt.spot[i*2+1]+dy);
        }
        cairo_line_to(cr,gspt.spot[2]+dx,gspt.spot[3]+dy);
      }
      else
      {
        cairo_move_to(cr,gspt.spot[2],gspt.spot[3]);
        for (int i=2; i<gspt.spot_count; i++)
        {
          cairo_line_to(cr,gspt.spot[i*2],gspt.spot[i*2+1]);
        }
        cairo_line_to(cr,gspt.spot[2],gspt.spot[3]);
        spt_x = gspt.spot[0];
        spt_y = gspt.spot[1];
      }
      cairo_stroke_preserve(cr);
      if(i == g->selected || i == g->dragging) cairo_set_line_width(cr, 2.0/zoom_scale);
      else                                     cairo_set_line_width(cr, 1.0/zoom_scale);
      cairo_set_source_rgba(cr, .8, .8, .8, .8);
      cairo_stroke(cr);
    }
    
    //line between
    cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
    if(i == g->selected || i == g->dragging) cairo_set_line_width(cr, 5.0/zoom_scale);
    else                                     cairo_set_line_width(cr, 3.0/zoom_scale);
    cairo_set_source_rgba(cr, .3, .3, .3, .8);
    cairo_move_to(cr,src_x,src_y);
    cairo_line_to(cr,spt_x,spt_y);
    cairo_stroke_preserve(cr);
    if(i == g->selected || i == g->dragging) cairo_set_line_width(cr, 2.0/zoom_scale);
    else                                     cairo_set_line_width(cr, 1.0/zoom_scale);
    cairo_set_source_rgba(cr, .8, .8, .8, .8);
    cairo_stroke(cr);
  }
}

int mouse_moved(dt_iop_module_t *self, double x, double y, int which)
{
  dt_iop_spots_params_t   *p = (dt_iop_spots_params_t   *)self->params;
  dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  // draw line (call post expose)
  // highlight selected point, if any
  float pzx, pzy;
  dt_dev_get_pointer_zoom_pos(self->dev, x, y, &pzx, &pzy);
  float wd = self->dev->preview_pipe->backbuf_width;
  float ht = self->dev->preview_pipe->backbuf_height;
  float rf = MIN(wd,ht);
  pzx += 0.5f;
  pzy += 0.5f;
  float mind = FLT_MAX;
  int selected = -1;
  const int old_sel = g->selected;
  gboolean hoover_c = g->hoover_c;
  gboolean old_border = g->border;
  g->selected = -1;
  g->border = FALSE;
  
  if(g->dragging < 0) for(int i=0; i<p->num_spots; i++)
    {
      if (!g->spot[i].ok) continue;
      
      float dist = (pzx*wd - g->spot[i].spot[0])*(pzx*wd - g->spot[i].spot[0]) + (pzy*ht - g->spot[i].spot[1])*(pzy*ht - g->spot[i].spot[1]);
      if(dist < mind)
      {
        mind = dist;
        selected = i;
        hoover_c = FALSE;
      }
      dist = (pzx*wd - g->spot[i].source[0])*(pzx*wd - g->spot[i].source[0]) + (pzy*ht - g->spot[i].source[1])*(pzy*ht - g->spot[i].source[1]);
      if(dist < mind)
      {
        mind = dist;
        selected = i;
        hoover_c = TRUE;
      }
    }
  else
  {
    if(g->hoover_c)
    {
      p->spot[g->dragging].source[0] = pzx;
      p->spot[g->dragging].source[1] = pzy;
    }
    else
    {
      g->form[g->dragging]->center[0] = pzx;
      g->form[g->dragging]->center[0] = pzy;
    }
  }
  if(selected >= 0 && mind < g->form[selected]->radius * g->form[selected]->radius * rf*rf)
  {
    g->selected = selected;
    g->hoover_c = hoover_c;
  }
  else if(selected >= 0 && p->spot[selected].version > 1 && mind < (g->form[selected]->radius + g->form[selected]->border) * (g->form[selected]->radius + g->form[selected]->border) * rf*rf)
  {
    g->selected = selected;
    g->border = TRUE;
    g->hoover_c = hoover_c;
  }
  if(g->dragging >= 0 || g->selected != old_sel || g->border != old_border) dt_control_queue_redraw_center();
  return 1;
}

int scrolled(dt_iop_module_t *self, double x, double y, int up, uint32_t state)
{
  dt_iop_spots_params_t   *p = (dt_iop_spots_params_t   *)self->params;
  dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  if(g->selected >= 0)
  {
    //we get the spot
    dt_masks_form_t *form = dt_masks_get_from_id(self->dev,p->spot[g->selected].formid);
    if (!form) return 0;
    dt_masks_point_circle_t *circle = dt_masks_get_circle(form);
    if (!circle) return 0;
    
    if ((state&GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
    {
      if(up && p->spot[g->selected].opacity > 0.0f) p->spot[g->selected].opacity -= 0.05f;
      else  if(p->spot[g->selected].opacity < 1.0f  ) p->spot[g->selected].opacity += 0.05f;
    }
    else if (g->border)
    {
      if(up && circle->border > 0.002f) circle->border *= 0.9f;
      else  if(circle->border < 0.1f  ) circle->border *= 1.0f/0.9f;
      dt_masks_write_form(form,self->dev);
      p->change = (p->change+1) % 1000;
      gui_spot_update_radius(self,&g->spot[g->selected],g->selected);
    }
    else
    {
      if(up && circle->radius > 0.002f) circle->radius *= 0.9f;
      else  if(circle->radius < 0.1f  ) circle->radius *= 1.0f/0.9f;
      dt_masks_write_form(form,self->dev);
      p->change = (p->change+1) % 1000;
      gui_spot_update_radius(self,&g->spot[g->selected],g->selected);
      g->last_radius = circle->radius;
      dt_conf_set_float("ui_last/spot_size", g->last_radius);
    }
    dt_dev_add_history_item(darktable.develop, self, TRUE);
    return 1;
  }
  return 0;
}

int button_pressed(dt_iop_module_t *self, double x, double y, int which, int type, uint32_t state)
{
  // set new point, select it, start drag
  if(self->off) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->off), 1);
  dt_iop_spots_params_t   *p = (dt_iop_spots_params_t   *)self->params;
  dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  if(which == 1)
  {
    float pzx, pzy;
    dt_dev_get_pointer_zoom_pos(self->dev, x, y, &pzx, &pzy);
    pzx += 0.5f;
    pzy += 0.5f;

    if(g->selected < 0)
    {
      if(p->num_spots == 32)
      {
        dt_control_log(_("spot removal only supports up to 32 spots"));
        return 1;
      }
      
      const int i = p->num_spots++;
      g->dragging = i;
      // on *wd|*ht scale, radius on *min(wd, ht).
      float wd = self->dev->preview_pipe->backbuf_width;
      float ht = self->dev->preview_pipe->backbuf_height;
      dt_masks_form_t *form = dt_masks_create(DT_MASKS_CIRCLE);
      dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *) malloc(sizeof(dt_masks_point_circle_t));
      form->points = g_list_append(form->points,circle);
      circle->radius = g->last_radius;
      circle->border = g->last_radius/2.0f;
      p->spot[i].formid = form->formid;
      p->spot[i].opacity = 1.0f;
      p->spot[i].version = 2;
      float pts[2] = {pzx*wd,pzy*ht};
      dt_dev_distort_backtransform(self->dev,pts,1);
      circle->center[0] = p->spot[i].source[0] = pts[0]/self->dev->preview_pipe->iwidth;
      circle->center[1] = p->spot[i].source[1] = pts[1]/self->dev->preview_pipe->iheight;
      self->dev->forms = g_list_append(self->dev->forms,form);
      dt_masks_write_form(form,self->dev);
      g->form[i] = circle;
      gui_spot_add(self,&g->spot[i],i);
      g->selected = i;
      g->hoover_c = TRUE;
      
    }
    else
    {
      g->dragging = g->selected;
      if (g->hoover_c)
      {
        p->spot[g->selected].source[0] = pzx;
        p->spot[g->selected].source[1] = pzy;
      }
      else
      {
        g->form[g->selected]->center[0] = pzx;
        g->form[g->selected]->center[1] = pzy;
      }
    }
    return 1;
  }
  return 0;
}

int button_released(struct dt_iop_module_t *self, double x, double y, int which, uint32_t state)
{
  // end point, stop drag
  dt_iop_spots_params_t   *p = (dt_iop_spots_params_t   *)self->params;
  dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  if(which == 1 && g->dragging >= 0)
  {
    float pzx, pzy;
    dt_dev_get_pointer_zoom_pos(self->dev, x, y, &pzx, &pzy);
    pzx += 0.5f;
    pzy += 0.5f;
    const int i = g->dragging;
    float wd = self->dev->preview_pipe->backbuf_width;
    float ht = self->dev->preview_pipe->backbuf_height;
    float pts[2] = {pzx*wd,pzy*ht};
    dt_dev_distort_backtransform(self->dev,pts,1);
    if(g->hoover_c)
    {
      p->spot[i].source[0] = pts[0]/self->dev->preview_pipe->iwidth;
      p->spot[i].source[1] = pts[1]/self->dev->preview_pipe->iheight;
      gui_spot_update_source(self,&g->spot[i],i);
    }
    else
    {
      //we get the spot
      dt_masks_form_t *form = dt_masks_get_from_id(self->dev,p->spot[i].formid);
      if (!form) return 0;;
      dt_masks_point_circle_t *circle = dt_masks_get_circle(form);
      if (!circle) return 0;
      circle->center[0] = pts[0]/self->dev->preview_pipe->iwidth;
      circle->center[1] = pts[1]/self->dev->preview_pipe->iheight;
      dt_masks_write_form(form,self->dev);
      p->change = (p->change+1) % 1000;
      gui_spot_update_spot(self,&g->spot[i],i);
    }
    g->selected = -1;
    dt_dev_add_history_item(darktable.develop, self, TRUE);
    g->dragging = -1;
    char str[3];
    snprintf(str,3,"%d",p->num_spots);
    gtk_label_set_text(g->label, str);

    return 1;
  }
  else if(which == 3 && g->selected >= 0)
  {
    // remove brush stroke
    const int i = --(p->num_spots);
    if(i > 0)
    {
      memcpy(p->spot + g->selected, p->spot + i, sizeof(spot_t));
      gui_spot_remove(self,&g->spot[g->selected],g->selected);
      memcpy(g->spot + g->selected, g->spot + i, sizeof(spot_draw_t));
    }
    dt_dev_add_history_item(darktable.develop, self, TRUE);
    g->selected = -1;
    char str[3];
    snprintf(str,3,"%d",p->num_spots);
    gtk_label_set_text(g->label, str);
  }
  return 0;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
