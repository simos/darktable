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
#include "develop/masks/circle.c"
#include "develop/masks/bezier.c"

void dt_masks_gui_form_create(dt_iop_module_t *module, dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  gui->pipe_hash = gui->formid = gui->points_count = gui->border_count = 0;
  
  if (dt_masks_get_points(module->dev,form, &gui->points, &gui->points_count,0,0))
  {
    if (dt_masks_get_border(module->dev,form, &gui->border, &gui->border_count,0,0))
    {
      gui->pipe_hash = module->dev->preview_pipe->backbuf_hash;
      gui->formid = form->formid;
    }
  }
}
void dt_masks_gui_form_remove(dt_iop_module_t *module, dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  gui->pipe_hash = gui->formid = gui->points_count = gui->border_count = 0;
  if (gui->points) free(gui->points);
  gui->points = NULL;
  if (gui->border) free(gui->border);
  gui->border = NULL;
}

void dt_masks_gui_form_update_border(dt_iop_module_t *module, dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  float *border;
  int border_count;
  
  if (dt_masks_get_border(module->dev,form, &border, &border_count,0,0))
  {
    if (gui->border) free(gui->border);
    gui->border = border;
    gui->border_count = border_count;
  }
}

void dt_masks_gui_form_test_create(dt_iop_module_t *module, dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  //we test if the image has changed
  if (gui->pipe_hash > 0)
  {
    if (gui->pipe_hash != module->dev->preview_pipe->backbuf_hash)
    {
      dt_masks_gui_form_remove(module,form,gui);
    }
  }
  
  //we create the spots if needed
  if (gui->pipe_hash == 0)
  {
    dt_masks_gui_form_create(module,form,gui);
  }
}

void dt_masks_gui_form_save_creation(dt_iop_module_t *module, dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  module->dev->forms = g_list_append(module->dev->forms,form);
  gui->creation = FALSE;
  
  //update params
  int forms_count = module->blend_params->forms_count;
  module->blend_params->forms[forms_count] = form->formid;
  module->blend_params->forms_state[forms_count] = DT_MASKS_STATE_SHOW | DT_MASKS_STATE_USE;
  if (form->type == DT_MASKS_CIRCLE) snprintf(form->name,128,"mask circle #%d",forms_count);
  else if (form->type == DT_MASKS_BEZIER) snprintf(form->name,128,"mask curve #%d",forms_count);
  dt_masks_write_form(form,module->dev);
  
  //update gui
  dt_iop_gui_blend_data_t *bd = (dt_iop_gui_blend_data_t*)module->blend_data;
  bd->form_label[forms_count] = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(bd->form_label[forms_count]), gtk_label_new(form->name));
  gtk_widget_show_all(bd->form_label[forms_count]);
  g_object_set_data(G_OBJECT(bd->form_label[forms_count]), "form", GUINT_TO_POINTER(form->formid));
  gtk_box_pack_start(GTK_BOX(bd->form_box), bd->form_label[forms_count], TRUE, TRUE,0);
  g_signal_connect(G_OBJECT(bd->form_label[forms_count]), "button-press-event", G_CALLBACK(dt_iop_gui_blend_setform_callback), module);
  GtkStyle *style = gtk_widget_get_style(bd->form_label[forms_count]);
  gtk_widget_modify_bg(bd->form_label[forms_count], GTK_STATE_SELECTED, &style->bg[GTK_STATE_NORMAL]);
  
  module->blend_params->forms_count++;
  
  //show the form if needed
  module->dev->form_gui->formid = form->formid;
}

int dt_masks_get_points(dt_develop_t *dev, dt_masks_form_t *form, float **points, int *points_count, float dx, float dy)
{
  if (form->type == DT_MASKS_CIRCLE)
  {
    dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *) (g_list_first(form->points)->data);
    return _circle_get_points(dev,circle->center[0]-dx, circle->center[1]-dy, circle->radius, points, points_count);
  }
  else if (form->type == DT_MASKS_BEZIER)
  {
    return _curve_get_points(dev,form, points, points_count);
  }
  return 0;
}

int dt_masks_get_border(dt_develop_t *dev, dt_masks_form_t *form, float **border, int *border_count, float dx, float dy)
{
  if (form->type == DT_MASKS_CIRCLE)
  {
    dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *) (g_list_first(form->points)->data);
    return _circle_get_points(dev,circle->center[0]-dx, circle->center[1]-dy, circle->radius + circle->border, border, border_count); 
  }
    else if (form->type == DT_MASKS_BEZIER)
  {
    return _curve_get_border(dev,form, border, border_count);
  }
  return 0;
}

int dt_masks_get_area(dt_iop_module_t *module, dt_dev_pixelpipe_iop_t *piece, dt_masks_form_t *form, int *width, int *height, int *posx, int *posy)
{
  if (form->type == DT_MASKS_CIRCLE)
  {
    return _circle_get_area(module,piece,form,width,height,posx,posy);
  }
  else if (form->type == DT_MASKS_BEZIER)
  {
    return _curve_get_area(module,piece,form,width,height,posx,posy);
  }
  return 0;  
}

int dt_masks_get_mask(dt_iop_module_t *module, dt_dev_pixelpipe_iop_t *piece, dt_masks_form_t *form, float **buffer, int *width, int *height, int *posx, int *posy)
{
  if (form->type == DT_MASKS_CIRCLE)
  {
    return _circle_get_mask(module,piece,form,buffer,width,height,posx,posy);
  }
  else if (form->type == DT_MASKS_BEZIER)
  {
    return _curve_get_mask(module,piece,form,buffer,width,height,posx,posy);
  }
  return 0; 
}

dt_masks_point_circle_t *dt_masks_get_circle(dt_masks_form_t *form)
{
  if (form->type == DT_MASKS_CIRCLE) return (dt_masks_point_circle_t *) (g_list_first(form->points)->data);
  return NULL;
}

dt_masks_form_t *dt_masks_create(dt_masks_type_t type)
{
  dt_masks_form_t *form = (dt_masks_form_t *)malloc(sizeof(dt_masks_form_t));
  form->type = type;
  form->version = 1;
  form->formid = time(NULL);
  
  form->points = NULL;
  
  return form;
}

dt_masks_form_t *dt_masks_get_from_id(dt_develop_t *dev, int id)
{
  GList *forms = g_list_first(dev->forms);
  while (forms)
  {
    dt_masks_form_t *form = (dt_masks_form_t *) forms->data;
    if (form->formid == id) return form;
    forms = g_list_next(forms);
  }
  return NULL;
}


void dt_masks_read_forms(dt_develop_t *dev)
{
  //first we have to remove all existant entries from the list
  if (dev->forms)
  {
    GList *forms = g_list_first(dev->forms);
    while (forms)
    {
      dt_masks_free_form((dt_masks_form_t *)forms->data);
      forms = g_list_next(forms);
    }
    g_list_free(dev->forms);
    dev->forms = NULL;
  }
  
  if(dev->image_storage.id <= 0) return;

  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
      "select imgid, formid, form, name, version, points, points_count from mask where imgid = ?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, dev->image_storage.id);
  
  while(sqlite3_step(stmt) == SQLITE_ROW)
  {
    // db record:
    // 0-img, 1-formid, 2-form_type, 3-name, 4-version, 5-points, 6-points_count
    
    //we get the values
    dt_masks_form_t *form = (dt_masks_form_t *)malloc(sizeof(dt_masks_form_t));
    form->formid = sqlite3_column_int(stmt, 1);
    form->type = sqlite3_column_int(stmt, 2);
    const char *name = (const char *)sqlite3_column_text(stmt, 3);
    snprintf(form->name,128,"%s",name);
    form->version = sqlite3_column_int(stmt, 4);
    form->points = NULL;
    int nb_points = sqlite3_column_int(stmt, 6);
    
    //and now we "read" the blob
    if (form->type == DT_MASKS_CIRCLE)
    {
      dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *)malloc(sizeof(dt_masks_point_circle_t));
      memcpy(circle, sqlite3_column_blob(stmt, 5), sizeof(dt_masks_point_circle_t));
      form->points = g_list_append(form->points,circle);
    }
    else if(form->type == DT_MASKS_BEZIER)
    {
      dt_masks_point_bezier_t *ptbuf = (dt_masks_point_bezier_t *)malloc(nb_points*sizeof(dt_masks_point_bezier_t));
      memcpy(ptbuf, sqlite3_column_blob(stmt, 5), nb_points*sizeof(dt_masks_point_bezier_t));
      for (int i=0; i<nb_points; i++)
        form->points = g_list_append(form->points,ptbuf+i);
    }
    
    //and we can add the form to the list
    dev->forms = g_list_append(dev->forms,form);
  }
  
  sqlite3_finalize (stmt);  
}

void dt_masks_write_form(dt_masks_form_t *form, dt_develop_t *dev)
{
  //we first erase all masks for the image present in the db
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "delete from mask where imgid = ?1 and formid = ?2", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, dev->image_storage.id);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, form->formid);
  sqlite3_step(stmt);
  sqlite3_finalize (stmt);
  
  //and we write the form
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "insert into mask (imgid, formid, form, name, version, points, points_count) values (?1, ?2, ?3, ?4, ?5, ?6, ?7)", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, dev->image_storage.id);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, form->formid);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 3, form->type);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 4, form->name, strlen(form->name), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 5, form->version);
  if (form->type == DT_MASKS_CIRCLE)
  {
    dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *) (g_list_first(form->points)->data);
    DT_DEBUG_SQLITE3_BIND_BLOB(stmt, 6, circle, sizeof(dt_masks_point_circle_t), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 7, 1);
    sqlite3_step (stmt);
    sqlite3_finalize (stmt);
  }
  else if (form->type == DT_MASKS_BEZIER)
  {
    int nb = g_list_length(form->points);
    dt_masks_point_bezier_t *ptbuf = (dt_masks_point_bezier_t *)malloc(nb*sizeof(dt_masks_point_bezier_t));
    GList *points = g_list_first(form->points);
    int pos=0;
    while(points)
    {
      dt_masks_point_bezier_t *pt = (dt_masks_point_bezier_t *)points->data;
      ptbuf[pos++] = *pt;
      points = g_list_next(points);
    }
    DT_DEBUG_SQLITE3_BIND_BLOB(stmt, 6, ptbuf, nb*sizeof(dt_masks_point_bezier_t), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 7, nb);
    sqlite3_step (stmt);
    sqlite3_finalize (stmt);
    free(ptbuf);
  }
}

void dt_masks_write_forms(dt_develop_t *dev)
{
  //we first erase all masks for the image present in the db
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "delete from mask where imgid = ?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, dev->image_storage.id);
  sqlite3_step(stmt);
  sqlite3_finalize (stmt);
  
  //and now we write each forms
  GList *forms = g_list_first(dev->forms);
  while (forms)
  {
    dt_masks_form_t *form = (dt_masks_form_t *) forms->data;

    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "insert into mask (imgid, formid, form, name, version, points, points_count) values (?1, ?2, ?3, ?4, ?5, ?6, ?7)", -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, dev->image_storage.id);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, form->formid);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 3, form->type);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 4, form->name, strlen(form->name), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 5, form->version);
    if (form->type == DT_MASKS_CIRCLE)
    {
      dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *) (g_list_first(form->points)->data);
      DT_DEBUG_SQLITE3_BIND_BLOB(stmt, 6, circle, sizeof(dt_masks_point_circle_t), SQLITE_TRANSIENT);
      DT_DEBUG_SQLITE3_BIND_INT(stmt, 7, 1);
      sqlite3_step (stmt);
      sqlite3_finalize (stmt);
    }
    else if (form->type == DT_MASKS_BEZIER)
    {
      int nb = g_list_length(form->points);
      dt_masks_point_bezier_t *ptbuf = (dt_masks_point_bezier_t *)malloc(nb*sizeof(dt_masks_point_bezier_t));
      GList *points = g_list_first(form->points);
      int pos=0;
      while(points)
      {
        dt_masks_point_bezier_t *pt = (dt_masks_point_bezier_t *)points->data;
        ptbuf[pos++] = *pt;
        points = g_list_next(points);
      }
      DT_DEBUG_SQLITE3_BIND_BLOB(stmt, 6, ptbuf, nb*sizeof(dt_masks_point_bezier_t), SQLITE_TRANSIENT);
      DT_DEBUG_SQLITE3_BIND_INT(stmt, 7, nb);
      sqlite3_step (stmt);
      sqlite3_finalize (stmt);
      free(ptbuf);
    }
    forms = g_list_next(forms);
  }  
}

void dt_masks_free_form(dt_masks_form_t *form)
{
  if (!form) return;
  g_list_free(form->points);
  free(form);
  form = NULL;
}

int dt_masks_events_mouse_moved (struct dt_iop_module_t *module, double x, double y, int which)
{
  if (!module) return 0;
  dt_masks_form_t *form = module->dev->form_visible;
  dt_masks_form_gui_t *gui = module->dev->form_gui;
  
  float pzx, pzy;
  dt_dev_get_pointer_zoom_pos(module->dev, x, y, &pzx, &pzy);
  pzx += 0.5f;
  pzy += 0.5f;
  
  if (form->type == DT_MASKS_CIRCLE) return _circle_events_mouse_moved(module,pzx,pzy,which,form,gui);
  else if (form->type == DT_MASKS_BEZIER) return _curve_events_mouse_moved(module,pzx,pzy,which,form,gui);
  return 0;
}
int dt_masks_events_button_released (struct dt_iop_module_t *module, double x, double y, int which, uint32_t state)
{
  if (!module) return 0;
  
  dt_masks_form_t *form = module->dev->form_visible;
  dt_masks_form_gui_t *gui = module->dev->form_gui;
  float pzx, pzy;
  dt_dev_get_pointer_zoom_pos(module->dev, x, y, &pzx, &pzy);
  pzx += 0.5f;
  pzy += 0.5f;
  
  if (form->type == DT_MASKS_CIRCLE) return _circle_events_button_released(module,pzx,pzy,which,state,form,gui);
  else if (form->type == DT_MASKS_BEZIER) return _curve_events_button_released(module,pzx,pzy,which,state,form,gui);
  
  return 0;
}

int dt_masks_events_button_pressed (struct dt_iop_module_t *module, double x, double y, int which, int type, uint32_t state)
{
  if (!module) return 0;
  dt_masks_form_t *form = module->dev->form_visible;
  dt_masks_form_gui_t *gui = module->dev->form_gui;  
  float pzx, pzy;
  dt_dev_get_pointer_zoom_pos(module->dev, x, y, &pzx, &pzy);
  pzx += 0.5f;
  pzy += 0.5f;
      
  if (form->type == DT_MASKS_CIRCLE) return _circle_events_button_pressed(module,pzx,pzy,which,type,state,form,gui);
  else if (form->type == DT_MASKS_BEZIER) return _curve_events_button_pressed(module,pzx,pzy,which,type,state,form,gui);
  
  return 0;
}

static void _curve_get_distance(float x, int y, float as, dt_masks_form_gui_t *gui, int corner_count, int *inside, int *inside_border, int *near)
{
  //we first check if it's inside borders
  int nb = 0;
  int last = -9999;
  *inside_border = 0;
  *near = -1;
  
  //and we check if it's inside form
  int seg = 1;
  for (int i=corner_count*3; i<gui->points_count; i++)
  {
    if (gui->points[i*2+1] == gui->points[seg*6+3] && gui->points[i*2] == gui->points[seg*6+2])
    {
      seg=(seg+1)%corner_count;
    }
    if (gui->points[i*2]-x < as && gui->points[i*2]-x > -as && gui->points[i*2+1]-y < as && gui->points[i*2+1]-y > -as)
    {
      if (seg == 0) *near = corner_count-1;
      else *near = seg-1;
    }
    int yy = (int) gui->points[i*2+1];
    if (yy != last && yy == y)
    {
      if (gui->points[i*2] > x) nb++;
    }
    last = yy;
  }
  *inside = (nb & 1);
}

int dt_masks_events_mouse_scrolled (struct dt_iop_module_t *module, double x, double y, int up, uint32_t state)
{
  if (!module) return 0;
  dt_masks_form_t *form = module->dev->form_visible;
  dt_masks_form_gui_t *gui = module->dev->form_gui;
  
  if (form->type == DT_MASKS_CIRCLE) return _circle_events_mouse_scrolled(module,0.0,0.0,up,state,form,gui);
  else if (form->type == DT_MASKS_BEZIER) return _curve_events_mouse_scrolled(module,0.0,0.0,up,state,form,gui);
  
  return 0;
}
void dt_masks_events_post_expose (struct dt_iop_module_t *module, cairo_t *cr, int32_t width, int32_t height, int32_t pointerx, int32_t pointery)
{
  if (!module) return;
  
  dt_develop_t *dev = module->dev;
  dt_masks_form_t *form = module->dev->form_visible;
  dt_masks_form_gui_t *gui = module->dev->form_gui;
  //if it's a spot in creation, nothing to draw
  if (form->type == DT_MASKS_CIRCLE && gui->creation) return;
  
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
  
  cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
  
  //we update the form if needed
  dt_masks_gui_form_test_create(module,form,gui);
    
  //draw form
  if (form->type == DT_MASKS_CIRCLE) _circle_events_post_expose(cr,zoom_scale,gui);
  else if (form->type == DT_MASKS_BEZIER) _curve_events_post_expose(cr,zoom_scale,gui,g_list_length(form->points));
}

void dt_masks_get_distance(float x, int y, float as, dt_masks_form_gui_t *gui, dt_masks_form_t *form, int *inside, int *inside_border, int *near)
{
  *inside = 0;
  *inside_border = 0;
  *near = -1;
  if (form->type == DT_MASKS_CIRCLE) _circle_get_distance(x,y,as,gui,inside,inside_border,near);
  else if (form->type == DT_MASKS_BEZIER) _curve_get_distance(x,y,as,gui,g_list_length(form->points),inside,inside_border,near);
}

void dt_masks_init_formgui(dt_develop_t *dev)
{
  dev->form_gui->pipe_hash = dev->form_gui->formid = dev->form_gui->points_count = dev->form_gui->border_count = 0;
  if (dev->form_gui->points) free(dev->form_gui->points);
  dev->form_gui->points = NULL;
  if (dev->form_gui->border) free(dev->form_gui->border);
  dev->form_gui->border = NULL;
  dev->form_gui->posx = dev->form_gui->posy = dev->form_gui->dx = dev->form_gui->dy = 0.0f;
  dev->form_gui->form_selected = dev->form_gui->border_selected = dev->form_gui->form_dragging = FALSE;
  dev->form_gui->seg_selected = dev->form_gui->point_selected = dev->form_gui->feather_selected = -1;
  dev->form_gui->seg_dragging = dev->form_gui->feather_dragging = dev->form_gui->point_dragging = -1;
  dev->form_gui->creation_closing_form = dev->form_gui->creation = FALSE;
  dev->form_gui->clockwise = TRUE;
}

static void _menu_form_add_circle(GtkButton *button, dt_iop_module_t *module)
{  
  //we create the new form
  dt_masks_form_t *spot = dt_masks_create(DT_MASKS_CIRCLE);
  dt_masks_init_formgui(module->dev);
  module->dev->form_visible = spot;
  module->dev->form_gui->creation = TRUE;

  //we remove visible selection on labels if any
  dt_iop_gui_blend_data_t *bd = (dt_iop_gui_blend_data_t *)module->blend_data;
  if (bd)
  {
    GList *childs = gtk_container_get_children(GTK_CONTAINER(bd->form_box));
    while(childs)
    {
      GtkWidget *w = (GtkWidget *) childs->data;
      gtk_widget_modify_bg(w, GTK_STATE_SELECTED, NULL);  
      childs = g_list_next(childs);
    } 
  }
  
  dt_control_queue_redraw_center();
}

static void _menu_form_add_bezier(GtkButton *button, dt_iop_module_t *module)
{  
  //we create the new form
  dt_masks_form_t *form = dt_masks_create(DT_MASKS_BEZIER);
  dt_masks_init_formgui(module->dev);
  module->dev->form_visible = form;
  module->dev->form_gui->creation = TRUE;

  //we remove visible selection on labels if any
  dt_iop_gui_blend_data_t *bd = (dt_iop_gui_blend_data_t *)module->blend_data;
  if (bd)
  {
    GList *childs = gtk_container_get_children(GTK_CONTAINER(bd->form_box));
    while(childs)
    {
      GtkWidget *w = (GtkWidget *) childs->data;
      gtk_widget_modify_bg(w, GTK_STATE_SELECTED, NULL);  
      childs = g_list_next(childs);
    } 
  }
  
  dt_control_queue_redraw_center();
}

static void _menu_form_add_existing(GtkButton *button, dt_iop_module_t *module)
{  
  //we get the new form
  int formid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "formid"));
  dt_masks_form_t *form = dt_masks_get_from_id(module->dev,formid);
  if (!form) return;
  
  //we select the new form
  dt_masks_init_formgui(module->dev);
  module->dev->form_visible = form;

  //we remove visible selection on labels if any
  dt_iop_gui_blend_data_t *bd = (dt_iop_gui_blend_data_t *)module->blend_data;
  if (bd)
  {
    GList *childs = gtk_container_get_children(GTK_CONTAINER(bd->form_box));
    while(childs)
    {
      GtkWidget *w = (GtkWidget *) childs->data;
      gtk_widget_modify_bg(w, GTK_STATE_SELECTED, NULL);  
      childs = g_list_next(childs);
    } 
  }
  
  //update params
  int forms_count = module->blend_params->forms_count;
  module->blend_params->forms[forms_count] = form->formid;
  module->blend_params->forms_state[forms_count] = DT_MASKS_STATE_SHOW | DT_MASKS_STATE_USE;
  dt_masks_write_form(form,module->dev);
  
  //update gui
  bd->form_label[forms_count] = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(bd->form_label[forms_count]), gtk_label_new(form->name));
  gtk_widget_show_all(bd->form_label[forms_count]);
  g_object_set_data(G_OBJECT(bd->form_label[forms_count]), "form", GUINT_TO_POINTER(forms_count));
  gtk_box_pack_start(GTK_BOX(bd->form_box), bd->form_label[forms_count], TRUE, TRUE,0);
  g_signal_connect(G_OBJECT(bd->form_label[forms_count]), "button-press-event", G_CALLBACK(dt_iop_gui_blend_setform_callback), module);
  GtkStyle *style = gtk_widget_get_style(bd->form_label[forms_count]);
  gtk_widget_modify_bg(bd->form_label[forms_count], GTK_STATE_SELECTED, &style->bg[GTK_STATE_NORMAL]);
  
  module->blend_params->forms_count++;  
  module->dev->form_gui->formid = form->formid;
  
  dt_dev_add_history_item(darktable.develop, module, TRUE);
  dt_control_queue_redraw_center();
}

GtkWidget *dt_masks_gui_get_menu(struct dt_iop_module_t *module)
{
  GtkWidget *menu = gtk_menu_new();
  GtkWidget *item;

  //add forms
  item = gtk_menu_item_new_with_label(_("add circular mask"));
  g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (_menu_form_add_circle), module);
  gtk_menu_append(menu, item);
  item = gtk_menu_item_new_with_label(_("add curve mask"));
  g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (_menu_form_add_bezier), module);
  gtk_menu_append(menu, item);
  
  //separator
  item = gtk_separator_menu_item_new();
  gtk_menu_append(menu, item);
  
  //existing forms
  GList *forms = g_list_first(module->dev->forms);
  while (forms)
  {
    dt_masks_form_t *form = (dt_masks_form_t *)forms->data;
    char str[10000] = "";
    strcat(str,form->name);
    int nbuse = 0;
    
    //we search were this form is used TODO
    GList *modules = g_list_first(module->dev->iop);
    while (modules)
    {
      dt_iop_module_t *m = (dt_iop_module_t *)modules->data;
      if (m->blend_params)
      {
        for (int i=0; i<m->blend_params->forms_count; i++)
        {
          if (m->blend_params->forms[i] == form->formid)
          {
            if (nbuse==0) strcat(str," (");
            strcat(str," ");
            strcat(str,m->name());
            nbuse++;
          }
        }
      }
      modules = g_list_next(modules);
    }
    if (nbuse>0) strcat(str," )");
    
    //we add the menu entry
    item = gtk_menu_item_new_with_label(str);
    g_object_set_data(G_OBJECT(item), "formid", GUINT_TO_POINTER(form->formid));
    g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (_menu_form_add_existing), module);
    gtk_menu_append(menu, item);
  
    forms = g_list_next(forms);
  }
  
  gtk_widget_show_all(menu);

  //we now create the main entry
  item = gtk_menu_item_new_with_label(_("masks"));
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),menu);
  gtk_widget_show_all(item);
  return item;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
