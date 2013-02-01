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

int dt_group_events_mouse_scrolled(struct dt_iop_module_t *module, float pzx, float pzy, int up, uint32_t state,
                                          dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  if (gui->group_edited >=0)
  {
    //we get the form
    dt_masks_point_group_t *fpt = (dt_masks_point_group_t *)g_list_nth_data(form->points,gui->group_edited);
    dt_masks_form_t *sel = dt_masks_get_from_id(module->dev,fpt->formid);
    if (!sel) return 0;
    if (sel->type == DT_MASKS_CIRCLE) return dt_circle_events_mouse_scrolled(module,pzx,pzy,up,state,form,gui,gui->group_edited);
    else if (sel->type == DT_MASKS_CURVE) return dt_curve_events_mouse_scrolled(module,pzx,pzy,up,state,form,gui,gui->group_edited);
  }
  return 0;
}

int dt_group_events_button_pressed(struct dt_iop_module_t *module,float pzx, float pzy, int which, int type, uint32_t state,
                                          dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  if (gui->group_edited != gui->group_selected)
  {
    //we set the selected form in edit mode
    gui->group_edited = gui->group_selected;
    //we initialise some variable
    gui->posx = gui->posy = gui->dx = gui->dy = 0.0f;
    gui->form_selected = gui->border_selected = gui->form_dragging = FALSE;
    gui->point_border_selected = gui->seg_selected = gui->point_selected = gui->feather_selected = -1;
    gui->point_border_dragging = gui->seg_dragging = gui->feather_dragging = gui->point_dragging = -1;

    dt_control_queue_redraw_center();
    return 1;
  }
  if (gui->group_edited >= 0)
  {
    //we get the form
    dt_masks_point_group_t *fpt = (dt_masks_point_group_t *)g_list_nth_data(form->points,gui->group_edited);
    dt_masks_form_t *sel = dt_masks_get_from_id(module->dev,fpt->formid);
    if (!sel) return 0;
    if (sel->type == DT_MASKS_CIRCLE) return dt_circle_events_button_pressed(module,pzx,pzy,which,type,state,form,gui,gui->group_edited);
    else if (sel->type == DT_MASKS_CURVE) return dt_curve_events_button_pressed(module,pzx,pzy,which,type,state,form,gui,gui->group_edited);
  }
  return 0;
}

int dt_group_events_button_released(struct dt_iop_module_t *module,float pzx, float pzy, int which, uint32_t state,
                                          dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  if (gui->group_edited >= 0)
  {
    //we get the form
    dt_masks_point_group_t *fpt = (dt_masks_point_group_t *)g_list_nth_data(form->points,gui->group_edited);
    dt_masks_form_t *sel = dt_masks_get_from_id(module->dev,fpt->formid);
    if (!sel) return 0;
    if (sel->type == DT_MASKS_CIRCLE) return dt_circle_events_button_released(module,pzx,pzy,which,state,form,gui,gui->group_edited);
    else if (sel->type == DT_MASKS_CURVE) return dt_curve_events_button_released(module,pzx,pzy,which,state,form,gui,gui->group_edited);
  }
  return 0;
}

int dt_group_events_mouse_moved(struct dt_iop_module_t *module,float pzx, float pzy, int which, dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  int32_t zoom, closeup;
  DT_CTL_GET_GLOBAL(zoom, dev_zoom);
  DT_CTL_GET_GLOBAL(closeup, dev_closeup);
  float zoom_scale = dt_dev_get_zoom_scale(module->dev, zoom, closeup ? 2 : 1, 1);
  float as = 0.005f/zoom_scale*module->dev->preview_pipe->backbuf_width;
  
  //if a form is in edit mode, we first execute the corresponding event
  if (gui->group_edited >= 0)
  {
    //we get the form
    dt_masks_point_group_t *fpt = (dt_masks_point_group_t *)g_list_nth_data(form->points,gui->group_edited);
    dt_masks_form_t *sel = dt_masks_get_from_id(module->dev,fpt->formid);
    if (!sel) return 0;
    int rep = 0;
    if (sel->type == DT_MASKS_CIRCLE) rep = dt_circle_events_mouse_moved(module,pzx,pzy,which,sel,gui,gui->group_edited);
    else if (sel->type == DT_MASKS_CURVE) rep = dt_curve_events_mouse_moved(module,pzx,pzy,which,sel,gui,gui->group_edited);
    if (rep) return 1;
  }
  
  //now we check if we are near a form
  GList *fpts = g_list_first(form->points);
  int pos = 0;
  gui->form_selected = gui->border_selected = FALSE;
  gui->feather_selected  = -1;
  gui->point_selected = -1;
  gui->seg_selected = -1;
  gui->point_border_selected = -1;
  gui->group_selected = -1;
  while(fpts)
  {
    dt_masks_point_group_t *fpt = (dt_masks_point_group_t *) fpts->data;
    dt_masks_form_t *sel = dt_masks_get_from_id(module->dev,fpt->formid);
    int inside, inside_border, near;
    inside = inside_border = 0;
    near = -1;
    float xx = pzx*module->dev->preview_pipe->backbuf_width, yy = pzy*module->dev->preview_pipe->backbuf_height;
    if (sel->type == DT_MASKS_CIRCLE) dt_circle_get_distance(xx,yy,as,gui,pos,&inside, &inside_border, &near);
    else if (sel->type == DT_MASKS_CURVE) dt_curve_get_distance(xx,yy,as,gui,pos,g_list_length(sel->points),&inside, &inside_border, &near);
    if (inside || inside_border || near >= 0)
    {
      gui->group_selected = pos;
      if (sel->type == DT_MASKS_CIRCLE) return dt_circle_events_mouse_moved(module,pzx,pzy,which,form,gui,pos);
      else if (sel->type == DT_MASKS_CURVE) return dt_curve_events_mouse_moved(module,pzx,pzy,which,form,gui,pos);
    }
    fpts = g_list_next(fpts);
    pos++;
  }
  dt_control_queue_redraw_center();
  return 0;
}

void dt_group_events_post_expose(cairo_t *cr,float zoom_scale,dt_masks_form_t *form,dt_masks_form_gui_t *gui)
{
  GList *fpts = g_list_first(form->points);
  int pos = 0;
  while(fpts)
  {
    dt_masks_point_group_t *fpt = (dt_masks_point_group_t *) fpts->data;
    dt_masks_form_t *sel = dt_masks_get_from_id(darktable.develop,fpt->formid);
    if (sel->type == DT_MASKS_CIRCLE) dt_circle_events_post_expose(cr,zoom_scale,gui,pos);
    else if (sel->type == DT_MASKS_CURVE) dt_curve_events_post_expose(cr,zoom_scale,gui,pos,g_list_length(sel->points));
    fpts = g_list_next(fpts);
    pos++;
  }
}

int dt_group_get_area(dt_iop_module_t *module, dt_dev_pixelpipe_iop_t *piece, dt_masks_form_t *form, int *width, int *height, int *posx, int *posy)
{  
  return 0;
}

int dt_group_get_mask(dt_iop_module_t *module, dt_dev_pixelpipe_iop_t *piece, dt_masks_form_t *form, float **buffer, int *width, int *height, int *posx, int *posy)
{
  return 0;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
