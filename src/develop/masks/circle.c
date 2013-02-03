/*
    This file is part of darktable,
    copyright (c) 2012 aldric renaudin.

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
#include "develop/imageop.h"
#include "develop/blend.h"
#include "control/control.h"
#include "control/conf.h"
#include "develop/masks.h"
#include "common/debug.h"

void dt_circle_get_distance(float x, int y, float as, dt_masks_form_gui_t *gui, int index, int *inside, int *inside_border, int *near)
{
  //we first check if it's inside borders
  int nb = 0;
  int last = -9999;
  dt_masks_form_gui_points_t *gpt = (dt_masks_form_gui_points_t *) g_list_nth_data(gui->points,index);
  
  for (int i=0; i<gpt->border_count; i++)
  {
    int yy = (int) gpt->border[i*2+1];
    if (yy != last && yy == y)
    {
      if (gpt->border[i*2] > x) nb++;
    }
    last = yy;
  }  
  if (!(nb & 1))
  {
    *inside = 0;
    *inside_border = 0;
    *near = -1;
    return;
  }
  *inside = 1;
  *near = 0;
  
  //and we check if it's inside form
  nb = 0;
  last = -9999;
  for (int i=0; i<gpt->points_count; i++)
  {
    int yy = (int) gpt->points[i*2+1];
    if (yy != last && yy == y)
    {
      if (gpt->points[i*2] > x) nb++;
      if (gpt->points[i*2] - x < as && gpt->points[i*2] - x > -as) *near = 1;
    }
    last = yy;
  }
  *inside_border = !(nb & 1);
}

int dt_circle_events_mouse_scrolled(struct dt_iop_module_t *module, float pzx, float pzy, int up, uint32_t state,
                                          dt_masks_form_t *form, dt_masks_form_gui_t *gui, int index)
{
  if (gui->form_selected)
  {
    dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *) (g_list_first(form->points)->data);
    if (gui->border_selected)
    {
      if(up && circle->border > 0.002f) circle->border *= 0.9f;
      else  if(circle->border < 1.0f  ) circle->border *= 1.0f/0.9f;
      dt_masks_write_form(form,module->dev);
      dt_masks_gui_form_update_border(module,form,gui,index);
    }
    else
    {
      if(up && circle->radius > 0.002f) circle->radius *= 0.9f;
      else  if(circle->radius < 1.0f  ) circle->radius *= 1.0f/0.9f;
      dt_masks_write_form(form,module->dev);
      dt_masks_gui_form_remove(module,form,gui,index);
      dt_masks_gui_form_create(module,form,gui,index);
    }
    dt_dev_add_history_item(darktable.develop, module, TRUE);
    return 1;
  }
  return 0;
}

int dt_circle_events_button_pressed(struct dt_iop_module_t *module,float pzx, float pzy, int which, int type, uint32_t state,
                                          dt_masks_form_t *form, dt_masks_form_gui_t *gui, int index)
{
  if (which != 1) return 0;

  if (gui->form_selected && !gui->creation)
  {
    dt_masks_form_gui_points_t *gpt = (dt_masks_form_gui_points_t *) g_list_nth_data(gui->points,index);
    //we start the form dragging
    gui->form_dragging = TRUE;
    gui->posx = pzx*module->dev->preview_pipe->backbuf_width;
    gui->posy = pzy*module->dev->preview_pipe->backbuf_height;
    gui->dx = gpt->points[0] - gui->posx;
    gui->dy = gpt->points[1] - gui->posy;
    return 1;
  }
  else if (gui->creation)
  {
    //we create the circle
    dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *) (malloc(sizeof(dt_masks_point_circle_t)));
    
    //we change the center value
    
    float wd = module->dev->preview_pipe->backbuf_width;
    float ht = module->dev->preview_pipe->backbuf_height;
    float pts[2] = {pzx*wd,pzy*ht};
    dt_dev_distort_backtransform(module->dev,pts,1);
    circle->center[0] = pts[0]/module->dev->preview_pipe->iwidth;
    circle->center[1] = pts[1]/module->dev->preview_pipe->iheight;
    circle->radius = 0.1f;
    circle->border = 0.05f;
    form->points = g_list_append(form->points,circle);
    
    dt_masks_gui_form_save_creation(module,form,gui);
    
    //we recreate the form points
    //dt_masks_gui_form_remove(module,form,gui,index);
    //dt_masks_gui_form_create(module,form,gui,index);
    
    //we save the move
    dt_dev_add_history_item(darktable.develop, module, TRUE);
    
    //and we switch in edit mode to show all the forms
    dt_masks_set_edit_mode(module, TRUE);
    dt_iop_gui_update_blending(module);
    
    return 1;
  }
  return 0;
}

int dt_circle_events_button_released(struct dt_iop_module_t *module,float pzx, float pzy, int which, uint32_t state,
                                          dt_masks_form_t *form, dt_masks_form_gui_t *gui,int index)
{
  if (gui->form_dragging)
  {
    //we get the circle
    dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *) (g_list_first(form->points)->data);
    
    //we end the form dragging
    gui->form_dragging = FALSE;
    
    //we change the center value
    float wd = module->dev->preview_pipe->backbuf_width;
    float ht = module->dev->preview_pipe->backbuf_height;
    float pts[2] = {pzx*wd+gui->dx,pzy*ht+gui->dy};
    dt_dev_distort_backtransform(module->dev,pts,1);
    circle->center[0] = pts[0]/module->dev->preview_pipe->iwidth;
    circle->center[1] = pts[1]/module->dev->preview_pipe->iheight;
    dt_masks_write_form(form,module->dev);

    //we recreate the form points
    dt_masks_gui_form_remove(module,form,gui,index);
    dt_masks_gui_form_create(module,form,gui,index);
    
    //we save the move
    dt_dev_add_history_item(darktable.develop, module, TRUE);
    
    return 1;
  }
  return 0;
}

int dt_circle_events_mouse_moved(struct dt_iop_module_t *module,float pzx, float pzy, int which, dt_masks_form_t *form, dt_masks_form_gui_t *gui, int index)
{
  if (gui->form_dragging)
  {
    gui->posx = pzx*module->dev->preview_pipe->backbuf_width;
    gui->posy = pzy*module->dev->preview_pipe->backbuf_height;
    dt_control_queue_redraw_center();
    return 1;
  }
  else if (!gui->creation)
  {
    int32_t zoom, closeup;
    DT_CTL_GET_GLOBAL(zoom, dev_zoom);
    DT_CTL_GET_GLOBAL(closeup, dev_closeup);
    float zoom_scale = dt_dev_get_zoom_scale(module->dev, zoom, closeup ? 2 : 1, 1);
    float as = 0.005f/zoom_scale*module->dev->preview_pipe->backbuf_width;
    int in,inb,near;
    dt_circle_get_distance(pzx*module->dev->preview_pipe->backbuf_width,pzy*module->dev->preview_pipe->backbuf_height,as,gui,index,&in,&inb,&near);
    if (inb)
    {
      gui->form_selected = TRUE;
      gui->border_selected = TRUE;
    }
    else if (in)
    {
      gui->form_selected = TRUE;
      gui->border_selected = FALSE;
    }
    else
    {
      gui->form_selected = FALSE;
      gui->border_selected = FALSE;
    }
    dt_control_queue_redraw_center();
    if (!gui->form_selected && !gui->border_selected) return 0;
    return 1;
  }
  
  return 0;
}

void dt_circle_events_post_expose(cairo_t *cr,float zoom_scale,dt_masks_form_gui_t *gui,int index)
{
  double dashed[] = {4.0, 4.0};
  dashed[0] /= zoom_scale;
  dashed[1] /= zoom_scale;
  int len  = sizeof(dashed) / sizeof(dashed[0]);
  dt_masks_form_gui_points_t *gpt = (dt_masks_form_gui_points_t *) g_list_nth_data(gui->points,index);
  float dx=0, dy=0; 
  if ((gui->group_selected == index) && gui->form_dragging)
  {
    dx = gui->posx + gui->dx - gpt->points[0];
    dy = gui->posy + gui->dy - gpt->points[1];
  }
  
  if (gpt->points_count > 6)
  { 
    cairo_set_dash(cr, dashed, 0, 0);     
    if ((gui->group_selected == index) && (gui->form_selected || gui->form_dragging)) cairo_set_line_width(cr, 5.0/zoom_scale);
    else                                     cairo_set_line_width(cr, 3.0/zoom_scale);
    cairo_set_source_rgba(cr, .3, .3, .3, .8);
    if (gui->form_dragging)
    {
      cairo_move_to(cr,gpt->points[2]+dx,gpt->points[3]+dy);
      for (int i=2; i<gpt->points_count; i++)
      {
        cairo_line_to(cr,gpt->points[i*2]+dx,gpt->points[i*2+1]+dy);
      }
      cairo_line_to(cr,gpt->points[2]+dx,gpt->points[3]+dy);
    }
    else
    {
      cairo_move_to(cr,gpt->points[2],gpt->points[3]);
      for (int i=2; i<gpt->points_count; i++)
      {
        cairo_line_to(cr,gpt->points[i*2],gpt->points[i*2+1]);
      }
      cairo_line_to(cr,gpt->points[2],gpt->points[3]);
    }
    cairo_stroke_preserve(cr);
    if ((gui->group_selected == index) && (gui->form_selected || gui->form_dragging)) cairo_set_line_width(cr, 2.0/zoom_scale);
    else                                     cairo_set_line_width(cr, 1.0/zoom_scale);
    cairo_set_source_rgba(cr, .8, .8, .8, .8);
    cairo_stroke(cr);
  }

  //draw border
  if ((gui->group_selected == index) && gpt->border_count > 6)
  { 
    cairo_set_dash(cr, dashed, len, 0);     
    if ((gui->group_selected == index) && (gui->border_selected)) cairo_set_line_width(cr, 2.0/zoom_scale);
    else                     cairo_set_line_width(cr, 1.0/zoom_scale);
    cairo_set_source_rgba(cr, .3, .3, .3, .8);
      
    cairo_move_to(cr,gpt->border[2],gpt->border[3]);
    for (int i=2; i<gpt->border_count; i++)
    {
      cairo_line_to(cr,gpt->border[i*2],gpt->border[i*2+1]);
    }
    cairo_line_to(cr,gpt->border[2],gpt->border[3]);

    cairo_stroke_preserve(cr);
    if ((gui->group_selected == index) && (gui->border_selected)) cairo_set_line_width(cr, 2.0/zoom_scale);
    else                     cairo_set_line_width(cr, 1.0/zoom_scale);
    cairo_set_source_rgba(cr, .8, .8, .8, .8);
    cairo_set_dash(cr, dashed, len, 4);
    cairo_stroke(cr);
  }
}

int dt_circle_get_points(dt_develop_t *dev, float x, float y, float radius, float **points, int *points_count)
{
  float wd = dev->preview_pipe->iwidth;
  float ht = dev->preview_pipe->iheight;

  //how many points do we need ?
  float r = radius*MIN(wd,ht);
  int l = (int) (2.0*M_PI*r);
  
  //buffer allocations
  *points = malloc(2*(l+1)*sizeof(float));
  *points_count = l+1;  
  
  //now we set the points
  (*points)[0] = x*wd;
  (*points)[1] = y*ht;
  for (int i=1; i<l+1; i++)
  {
    float alpha = (i-1)*2.0*M_PI/(float) l;
    (*points)[i*2] = (*points)[0] + r*cosf(alpha);
    (*points)[i*2+1] = (*points)[1] + r*sinf(alpha);
  }
  
  //and we transform them with all distorted modules
  if (dt_dev_distort_transform(dev,*points,l+1)) return 1;
  
  //if we failed, then free all and return
  free(*points);
  *points = NULL;
  *points_count = 0;
  return 0;  
}

int dt_circle_get_area(dt_iop_module_t *module, dt_dev_pixelpipe_iop_t *piece, dt_masks_form_t *form, int *width, int *height, int *posx, int *posy)
{  
  //we get the cicle values
  dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *) (g_list_first(form->points)->data);
  float wd = piece->pipe->iwidth, ht = piece->pipe->iheight;
  
  float r = (circle->radius + circle->border)*MIN(wd,ht);
  int l = (int) (2.0*M_PI*r);
  //buffer allocations
  float *points = malloc(2*(l+1)*sizeof(float)); 
  
  //now we set the points
  points[0] = circle->center[0]*wd;
  points[1] = circle->center[1]*ht;
  for (int i=1; i<l+1; i++)
  {
    float alpha = (i-1)*2.0*M_PI/(float) l;
    points[i*2] = points[0] + r*cosf(alpha);
    points[i*2+1] = points[1] + r*sinf(alpha);
  }
  
  //and we transform them with all distorted modules
  if (!dt_dev_distort_transform_plus(module->dev,piece->pipe,0,module->priority,points,l+1))
  {
    free(points);
    return 0;
  }
  
  //now we search min and max
  float xmin, xmax, ymin, ymax;
  xmin = ymin = FLT_MAX;
  xmax = ymax = FLT_MIN;
  for (int i=1; i < l+1; i++)
  {
    xmin = fminf(points[i*2],xmin);
    xmax = fmaxf(points[i*2],xmax);
    ymin = fminf(points[i*2+1],ymin);
    ymax = fmaxf(points[i*2+1],ymax);
  }
  
  //and we set values
  *posx = xmin;
  *posy = ymin;
  *width = (xmax-xmin);
  *height = (ymax-ymin);
  return 1;
}

int dt_circle_get_mask(dt_iop_module_t *module, dt_dev_pixelpipe_iop_t *piece, dt_masks_form_t *form, float **buffer, int *width, int *height, int *posx, int *posy)
{
  //we get the area
  if (!dt_circle_get_area(module,piece,form,width,height,posx,posy)) return 0;
  //float wd = scale*piece->buf_in.width, ht = scale*piece->buf_in.height;
  
  //we get the cicle values
  dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *) (g_list_first(form->points)->data);
  
  //we create a buffer of points with all points in the area
  int w = *width, h = *height;
  float *points = malloc(w*h*2*sizeof(float));
  for (int i=0; i<h; i++)
    for (int j=0; j<w; j++)
    {
      points[(i*w+j)*2] = (j+(*posx));
      points[(i*w+j)*2+1] = (i+(*posy));
    }
    
  //we back transform all this points
  if (!dt_dev_distort_backtransform_plus(module->dev,piece->pipe,0,module->priority,points,w*h)) return 0;
  
  //we allocate the buffer
  *buffer = malloc(w*h*sizeof(float));
  
  //we populate the buffer
  int wi = piece->pipe->iwidth, hi=piece->pipe->iheight;
  float center[2] = {circle->center[0]*wi, circle->center[1]*hi};
  float radius2 = circle->radius*MIN(wi,hi)*circle->radius*MIN(wi,hi);
  float total2 = (circle->radius+circle->border)*MIN(wi,hi)*(circle->radius+circle->border)*MIN(wi,hi);
  for (int i=0; i<h; i++)
    for (int j=0; j<w; j++)
    {
      float x = points[(i*w+j)*2];
      float y = points[(i*w+j)*2+1];
      float l2 = (x-center[0])*(x-center[0]) + (y-center[1])*(y-center[1]);
      if (l2<radius2) (*buffer)[i*w+j] = 1.0f;
      else if (l2 < total2)
      {
        float f = (total2-l2)/(total2-radius2);
        (*buffer)[i*w+j] = f*f;
      }
      else (*buffer)[i*w+j] = 0.0f;
    }
  free(points);
  return 1;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
