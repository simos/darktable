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

void _circle_draw(cairo_t *cr,float zoom_scale,dt_masks_form_gui_t *gui)
{
  double dashed[] = {4.0, 4.0};
  dashed[0] /= zoom_scale;
  dashed[1] /= zoom_scale;
  int len  = sizeof(dashed) / sizeof(dashed[0]);
  
  if (gui->points_count > 6)
  { 
    cairo_set_dash(cr, dashed, 0, 0);     
    if(gui->selected || gui->form_dragging) cairo_set_line_width(cr, 5.0/zoom_scale);
    else                                     cairo_set_line_width(cr, 3.0/zoom_scale);
    cairo_set_source_rgba(cr, .3, .3, .3, .8);
    if (gui->form_dragging)
    {
      float dx = gui->posx + gui->dx - gui->points[0], dy = gui->posy + gui->dy - gui->points[1];
      cairo_move_to(cr,gui->points[2]+dx,gui->points[3]+dy);
      for (int i=2; i<gui->points_count; i++)
      {
        cairo_line_to(cr,gui->points[i*2]+dx,gui->points[i*2+1]+dy);
      }
      cairo_line_to(cr,gui->points[2]+dx,gui->points[3]+dy);
    }
    else
    {
      cairo_move_to(cr,gui->points[2],gui->points[3]);
      for (int i=2; i<gui->points_count; i++)
      {
        cairo_line_to(cr,gui->points[i*2],gui->points[i*2+1]);
      }
      cairo_line_to(cr,gui->points[2],gui->points[3]);
    }
    cairo_stroke_preserve(cr);
    if(gui->selected || gui->form_dragging) cairo_set_line_width(cr, 2.0/zoom_scale);
    else                                     cairo_set_line_width(cr, 1.0/zoom_scale);
    cairo_set_source_rgba(cr, .8, .8, .8, .8);
    cairo_stroke(cr);
  }

  //draw border
  if ((!gui->form_dragging) && gui->border_count > 6)
  { 
    cairo_set_dash(cr, dashed, len, 0);     
    if(gui->border_selected) cairo_set_line_width(cr, 2.0/zoom_scale);
    else                     cairo_set_line_width(cr, 1.0/zoom_scale);
    cairo_set_source_rgba(cr, .3, .3, .3, .8);
      
    cairo_move_to(cr,gui->border[2],gui->border[3]);
    for (int i=2; i<gui->border_count; i++)
    {
      cairo_line_to(cr,gui->border[i*2],gui->border[i*2+1]);
    }
    cairo_line_to(cr,gui->border[2],gui->border[3]);

    cairo_stroke_preserve(cr);
    if(gui->border_selected) cairo_set_line_width(cr, 2.0/zoom_scale);
    else                     cairo_set_line_width(cr, 1.0/zoom_scale);
    cairo_set_source_rgba(cr, .8, .8, .8, .8);
    cairo_set_dash(cr, dashed, len, 4);
    cairo_stroke(cr);
  }
}

int _circle_get_points(dt_develop_t *dev, float x, float y, float radius, float **points, int *points_count)
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

int _circle_get_area(dt_iop_module_t *module, dt_dev_pixelpipe_iop_t *piece, dt_masks_form_t *form, int *width, int *height, int *posx, int *posy)
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
  if (!dt_dev_distort_transform_plus(module->dev,piece->pipe,0,module->priority,points,l+1)) return 0;
  
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

int _circle_get_mask(dt_iop_module_t *module, dt_dev_pixelpipe_iop_t *piece, dt_masks_form_t *form, float **buffer, int *width, int *height, int *posx, int *posy)
{
  //we get the area
  if (!_circle_get_area(module,piece,form,width,height,posx,posy)) return 0;
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

//Get the control points of a segment to match exactly a catmull-rom spline
static void catmull_to_bezier(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
                                float* bx1, float* by1, float* bx2, float* by2)
{
  *bx1 = (-x1 + 6*x2 + x3) / 6;
  *by1 = (-y1 + 6*y2 + y3) / 6;
  *bx2 = ( x2 + 6*x3 - x4) / 6;
  *by2 = ( y2 + 6*y3 - y4) / 6;
}

static void _curve_init_ctrl_points (dt_masks_form_t *form)
{
  //if we have less that 3 points, what to do ??
  if (g_list_length(form->points) < 2)
  {
    return;
  }
  
  int nb = g_list_length(form->points);
  for(int k = 0; k < nb; k++)
  {
    dt_masks_point_bezier_t *point3 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k);
    //if the point as not be set manually, we redfine it
    if (point3->state & DT_MASKS_POINT_STATE_NORMAL)
    {
      //we want to get point-2, point-1, point+1, point+2
      int k1,k2,k4,k5;
      k1 = (k-2)<0?nb+(k-2):k-2;
      k2 = (k-1)<0?nb-1:k-1;
      k4 = (k+1)%nb;
      k5 = (k+2)%nb;
      dt_masks_point_bezier_t *point1 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k1);
      dt_masks_point_bezier_t *point2 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k2);
      dt_masks_point_bezier_t *point4 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k4);
      dt_masks_point_bezier_t *point5 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k5);
      
      float bx1,by1,bx2,by2;
      catmull_to_bezier(point1->corner[0],point1->corner[1],
                        point2->corner[0],point2->corner[1],
                        point3->corner[0],point3->corner[1],
                        point4->corner[0],point4->corner[1],
                        &bx1,&by1,&bx2,&by2);
      if (point2->ctrl2[0] == -1.0) point2->ctrl2[0] = bx1;
      if (point2->ctrl2[1] == -1.0) point2->ctrl2[1] = by1;
      point3->ctrl1[0] = bx2;
      point3->ctrl1[1] = by2;
      catmull_to_bezier(point2->corner[0],point2->corner[1],
                        point3->corner[0],point3->corner[1],
                        point4->corner[0],point4->corner[1],
                        point5->corner[0],point5->corner[1],
                        &bx1,&by1,&bx2,&by2);
      if (point4->ctrl1[0] == -1.0) point4->ctrl1[0] = bx2;
      if (point4->ctrl1[1] == -1.0) point4->ctrl1[1] = by2;
      point3->ctrl2[0] = bx1;
      point3->ctrl2[1] = by1;
    }
  }
}

static gboolean _curve_is_clockwise(dt_masks_form_t *form)
{
  if (g_list_length(form->points) > 2)
  {
    float sum = 0.0f;
    int nb = g_list_length(form->points);
    for(int k = 0; k < nb; k++)
    {      
      int k2 = (k+1)%nb;
      dt_masks_point_bezier_t *point1 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k);
      dt_masks_point_bezier_t *point2 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k2);
      //edge k
      sum += (point2->corner[0]-point1->corner[0])*(point2->corner[1]+point1->corner[1]);
    }
    return (sum < 0);
  }
  //return dummy answer
  return TRUE;
}

static void _curve_points_recurs(float *p1, float *p2, 
                                  double tmin, double tmax, float minx, float miny, float maxx, float maxy, 
                                  float *rx, float *ry, float *tabl, int *pos)
{
  //we calcul points if needed
  if (minx == -99999)
  {
    minx = p1[0]*(1.0-tmin)*(1.0-tmin)*(1.0-tmin) +
            p1[2]*3*tmin*(1-tmin)*(1-tmin) +
            p2[2]*3*tmin*tmin*(1-tmin) +
            p2[0]*tmin*tmin*tmin;
    miny = p1[1]*(1.0-tmin)*(1.0-tmin)*(1.0-tmin) +
            p1[3]*3*tmin*(1-tmin)*(1-tmin) +
            p2[3]*3*tmin*tmin*(1-tmin) +
            p2[1]*tmin*tmin*tmin;
  }
  if (maxx == -99999)
  {
    maxx = p1[0]*(1.0-tmax)*(1.0-tmax)*(1.0-tmax) +
                        p1[2]*3*tmax*(1-tmax)*(1-tmax) +
                        p2[2]*3*tmax*tmax*(1-tmax) +
                        p2[0]*tmax*tmax*tmax;
    maxy = p1[1]*(1.0-tmax)*(1.0-tmax)*(1.0-tmax) +
                        p1[3]*3*tmax*(1-tmax)*(1-tmax) +
                        p2[3]*3*tmax*tmax*(1-tmax) +
                        p2[1]*tmax*tmax*tmax;
  }
  
  //are the point near ? (we just test y as it's the value we use for rendering
  if (miny-maxy <= 1 && miny-maxy >= -1 && minx-maxx <= 2 && minx-maxx >= -2)
  {
    tabl[*pos] = maxx;
    tabl[*pos+1] = maxy;
    *pos += 2;
    *rx = maxx;
    *ry = maxy;
    return;
  }
  
  //we split in two part
  double tx = (tmin+tmax)/2.0;
  float x,y;
  _curve_points_recurs(p1,p2,tmin,tx,minx,miny,-99999,-99999,&x,&y,tabl,pos);
  _curve_points_recurs(p1,p2,tx,tmax,x,y,maxx,maxy,rx,ry,tabl,pos);
}

int _curve_get_points(dt_develop_t *dev, dt_masks_form_t *form, float **points, int *points_count)
{
  float wd = dev->preview_pipe->iwidth;
  float ht = dev->preview_pipe->iheight;

  //we allocate buffer (very large) => how to handle this ???
  *points = malloc(60000*sizeof(float));
  
  //we store all points
  int nb = g_list_length(form->points);
  for(int k = 0; k < nb; k++)
  {
    dt_masks_point_bezier_t *pt = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k);
    (*points)[k*6] = pt->ctrl1[0]*wd;
    (*points)[k*6+1] = pt->ctrl1[1]*ht;
    (*points)[k*6+2] = pt->corner[0]*wd;
    (*points)[k*6+3] = pt->corner[1]*ht;
    (*points)[k*6+4] = pt->ctrl2[0]*wd;
    (*points)[k*6+5] = pt->ctrl2[1]*ht;
  }
  int pos = 6*nb;
  //we render all segments
  for(int k = 0; k < nb; k++)
  {
    int k2 = (k+1)%nb;
    dt_masks_point_bezier_t *point1 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k);
    dt_masks_point_bezier_t *point2 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k2);
    float p1[4] = {point1->corner[0]*wd, point1->corner[1]*ht, point1->ctrl2[0]*wd, point1->ctrl2[1]*ht};
    float p2[4] = {point2->corner[0]*wd, point2->corner[1]*ht, point2->ctrl1[0]*wd, point2->ctrl1[1]*ht};
    
    //we store the first point
    (*points)[pos++] = p1[0];
    (*points)[pos++] = p1[1];
    
    //and we determine all points by recursion (to be sure the distance between 2 points is <=1)
    float rx,ry;
    _curve_points_recurs(p1,p2,0.0,1.0,p1[0],p1[1],p2[0],p2[1],&rx,&ry,*points,&pos);
  }
  *points_count = pos/2;
  
  //and we transform them with all distorted modules
  if (dt_dev_distort_transform(dev,*points,*points_count)) return 1;
  
  //if we failed, then free all and return
  free(*points);
  *points = NULL;
  *points_count = 0;
  return 0; 
}

int _curve_get_border(dt_develop_t *dev, dt_masks_form_t *form, float **points, int *points_count)
{
  *points_count = 0;
  return 1;
}

void _curve_draw(cairo_t *cr, float zoom_scale, dt_masks_form_gui_t *gui, int nb)
{
  double dashed[] = {4.0, 4.0};
  dashed[0] /= zoom_scale;
  dashed[1] /= zoom_scale;
  //int len  = sizeof(dashed) / sizeof(dashed[0]);
  
  //draw curve
  if (gui->points_count > nb+6)
  { 
    cairo_set_dash(cr, dashed, 0, 0);     
    if(gui->selected || gui->form_dragging) cairo_set_line_width(cr, 5.0/zoom_scale);
    else                                     cairo_set_line_width(cr, 3.0/zoom_scale);
    cairo_set_source_rgba(cr, .3, .3, .3, .8);
    if (gui->form_dragging)
    {
      float dx = gui->posx + gui->dx - gui->points[2], dy = gui->posy + gui->dy - gui->points[3];
      cairo_move_to(cr,gui->points[nb*3*2]+dx,gui->points[nb*3*2+1]+dy);
      for (int i=nb*3*2; i<gui->points_count; i++)
      {
        cairo_line_to(cr,gui->points[i*2]+dx,gui->points[i*2+1]+dy);
      }
      cairo_line_to(cr,gui->points[nb*3*2]+dx,gui->points[nb*3*2+1]+dy);
    }
    else
    {
      cairo_move_to(cr,gui->points[nb*6],gui->points[nb*6+1]);
      for (int i=nb*3; i<gui->points_count; i++)
      {
        cairo_line_to(cr,gui->points[i*2],gui->points[i*2+1]);
      }
      cairo_line_to(cr,gui->points[nb*6],gui->points[nb*6+1]);
    }
    cairo_stroke_preserve(cr);
    if(gui->selected || gui->form_dragging) cairo_set_line_width(cr, 2.0/zoom_scale);
    else                                     cairo_set_line_width(cr, 1.0/zoom_scale);
    cairo_set_source_rgba(cr, .8, .8, .8, .8);
    cairo_stroke(cr);
  }
  
  //draw corners
  float anchor_size = 5.0f / zoom_scale;
  for(int k = 0; k < nb; k++)
  {
    //if (k == gui->point_dragging)
    {
      anchor_size = 6.0f / zoom_scale;
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
    }
    //else cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.5);
    
    cairo_rectangle(cr, 
        gui->points[k*6+2] - (anchor_size*0.5), 
        gui->points[k*6+3] - (anchor_size*0.5), 
        anchor_size, anchor_size);
    cairo_fill_preserve(cr);

    //if (k == gui->point_dragging) 
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.9);
    //else cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.5);
    cairo_stroke(cr);
  }
  
  //draw help lines
   cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 1.0);
    for(int k = 0; k < nb; k++)
    {
      //uncomment this part if you want to see "real" control points
      cairo_move_to(cr, gui->points[k*6+2],gui->points[k*6+3]);
      cairo_line_to(cr, gui->points[k*6],gui->points[k*6+1]);
      cairo_stroke(cr);
      cairo_move_to(cr, gui->points[k*6+2],gui->points[k*6+3]);
      cairo_line_to(cr, gui->points[k*6+4],gui->points[k*6+5]);
      cairo_stroke(cr);
    }
      
  //draw border
  //TODO
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
  return 0;  
}

int dt_masks_get_mask(dt_iop_module_t *module, dt_dev_pixelpipe_iop_t *piece, dt_masks_form_t *form, float **buffer, int *width, int *height, int *posx, int *posy)
{
  if (form->type == DT_MASKS_CIRCLE)
  {
    return _circle_get_mask(module,piece,form,buffer,width,height,posx,posy);
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
    //int nb_points = sqlite3_column_int(stmt, 6);
    
    //and now we "read" the blob
    if (form->type == DT_MASKS_CIRCLE)
    {
      dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *)malloc(sizeof(dt_masks_point_circle_t));
      memcpy(circle, sqlite3_column_blob(stmt, 5), sizeof(dt_masks_point_circle_t));
      form->points = g_list_append(form->points,circle);
    }
    else if(form->type == DT_MASKS_BEZIER)
    {
      //TODO
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
  }
  else if (form->type == DT_MASKS_BEZIER)
  {
    //TODO
  }
  
  sqlite3_step (stmt);
  sqlite3_finalize (stmt);
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
      dt_masks_point_circle_t *c = (dt_masks_point_circle_t *)g_list_first(form->points)->data;
      DT_DEBUG_SQLITE3_BIND_BLOB(stmt, 6, c, sizeof(dt_masks_point_circle_t), SQLITE_TRANSIENT);
      DT_DEBUG_SQLITE3_BIND_INT(stmt, 7, 1);
    }
    else if (form->type == DT_MASKS_BEZIER)
    {
      //TODO
    }
    
    sqlite3_step (stmt);
    sqlite3_finalize (stmt);
    forms = g_list_next(forms);
  }  
}

void dt_masks_free_form(dt_masks_form_t *form)
{
  if (!form) return;
  GList *points = g_list_first(form->points);
  while(points)
  {
    free(points->data);
    points->data = NULL;
    points = g_list_next(points);
  }
  g_list_free(form->points);
  free(form);
  form = NULL;
}

static void _gui_form_create(dt_iop_module_t *module, dt_masks_form_t *form, dt_masks_form_gui_t *gui)
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
static void _gui_form_remove(dt_iop_module_t *module, dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  gui->pipe_hash = gui->formid = gui->points_count = gui->border_count = 0;
  if (gui->points) free(gui->points);
  gui->points = NULL;
  if (gui->border) free(gui->border);
  gui->border = NULL;
}


static void _gui_form_update_border(dt_iop_module_t *module, dt_masks_form_t *form, dt_masks_form_gui_t *gui)
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

static int _gui_form_test_create(dt_iop_module_t *module, dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  //we test if the image has changed
  if (gui->pipe_hash > 0)
  {
    if (gui->pipe_hash != module->dev->preview_pipe->backbuf_hash)
    {
      _gui_form_remove(module,form,gui);
    }
  }
  
  //we create the spots if needed
  if (gui->pipe_hash == 0)
  {
    _gui_form_create(module,form,gui);
  }
  return 1;
}

static void _gui_form_save_creation(dt_iop_module_t *module, dt_masks_form_t *form, dt_masks_form_gui_t *gui)
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
  g_object_set_data(G_OBJECT(bd->form_label[forms_count]), "form", GUINT_TO_POINTER(forms_count));
  gtk_box_pack_start(GTK_BOX(bd->form_box), bd->form_label[forms_count], TRUE, TRUE,0);
  g_signal_connect(G_OBJECT(bd->form_label[forms_count]), "button-press-event", G_CALLBACK(dt_iop_gui_blend_setform_callback), module);
  GtkStyle *style = gtk_widget_get_style(bd->form_label[forms_count]);
  gtk_widget_modify_bg(bd->form_label[forms_count], GTK_STATE_SELECTED, &style->bg[GTK_STATE_NORMAL]);
  
  module->blend_params->forms_count++;
  
  //show the form if needed
  module->dev->form_gui->formid = form->formid;
}

int dt_masks_mouse_moved (struct dt_iop_module_t *module, double x, double y, int which)
{
  if (!module) return 0;
  dt_masks_form_t *form = module->dev->form_visible;
  dt_masks_form_gui_t *gui = module->dev->form_gui;
  
  float pzx, pzy;
  dt_dev_get_pointer_zoom_pos(module->dev, x, y, &pzx, &pzy);
  pzx += 0.5f;
  pzy += 0.5f;
      
  if (gui->form_dragging && which == 1)
  {
    gui->posx = pzx*module->dev->preview_pipe->backbuf_width;
    gui->posy = pzy*module->dev->preview_pipe->backbuf_height;
    dt_control_queue_redraw_center();
    return 1;
  }
  else if (gui->point_dragging >=0 && form->type == DT_MASKS_BEZIER)
  {
    float wd = module->dev->preview_pipe->backbuf_width;
    float ht = module->dev->preview_pipe->backbuf_height;
    float pts[2] = {pzx*wd,pzy*ht};
    dt_dev_distort_backtransform(module->dev,pts,1);
    dt_masks_point_bezier_t *bzpt = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,gui->point_dragging);
    pzx = pts[0]/module->dev->preview_pipe->iwidth;
    pzy = pts[1]/module->dev->preview_pipe->iheight;
    bzpt->ctrl1[0] += pzx - bzpt->corner[0];
    bzpt->ctrl2[0] += pzx - bzpt->corner[0];
    bzpt->ctrl1[1] += pzy - bzpt->corner[1];
    bzpt->ctrl2[1] += pzy - bzpt->corner[1];
    bzpt->corner[0] = pzx;
    bzpt->corner[1] = pzy;
    _curve_init_ctrl_points(form);
    //we recreate the form points
    _gui_form_remove(module,form,gui);
    _gui_form_create(module,form,gui);
    dt_control_queue_redraw_center();
    return 1;
  }
  else if (!gui->creation)
  {
    dt_masks_set_inside(pzx*module->dev->preview_pipe->backbuf_width,pzy*module->dev->preview_pipe->backbuf_height,gui);
    dt_control_queue_redraw_center();
    if (!gui->selected) return 0;
    return 1;
  }
  
  return 0;
}
int dt_masks_button_released (struct dt_iop_module_t *module, double x, double y, int which, uint32_t state)
{
  if (!module) return 0;
  if (which != 1) return 0;
  
  dt_masks_form_t *form = module->dev->form_visible;
  dt_masks_form_gui_t *gui = module->dev->form_gui;
  
  if (form->type == DT_MASKS_CIRCLE)
  {
    if (gui->form_dragging)
    {
      //we get the circle
      dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *) (g_list_first(form->points)->data);
      
      //we end the form dragging
      gui->form_dragging = FALSE;
      
      //we change the center value
      float pzx, pzy;
      dt_dev_get_pointer_zoom_pos(module->dev, x, y, &pzx, &pzy);
      pzx += 0.5f;
      pzy += 0.5f;
      float wd = module->dev->preview_pipe->backbuf_width;
      float ht = module->dev->preview_pipe->backbuf_height;
      float pts[2] = {pzx*wd+gui->dx,pzy*ht+gui->dy};
      dt_dev_distort_backtransform(module->dev,pts,1);
      circle->center[0] = pts[0]/module->dev->preview_pipe->iwidth;
      circle->center[1] = pts[1]/module->dev->preview_pipe->iheight;
      dt_masks_write_form(form,module->dev);

      //we recreate the form points
      _gui_form_remove(module,form,gui);
      _gui_form_create(module,form,gui);
      
      //we save the move
      dt_dev_add_history_item(darktable.develop, module, TRUE);
      
      return 1;
    }
  }
  
  return 0;
}
int dt_masks_button_pressed (struct dt_iop_module_t *module, double x, double y, int which, int type, uint32_t state)
{
  if (!module) return 0;
  dt_masks_form_t *form = module->dev->form_visible;
  dt_masks_form_gui_t *gui = module->dev->form_gui;  
  
  if (form->type == DT_MASKS_CIRCLE)
  {
    if (which != 1) return 0;
    if (gui->selected && !gui->creation)
    {
      //we start the form dragging
      gui->form_dragging = TRUE;
      
      //dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *) (g_list_first(form->points)->data);
      float pzx, pzy;
      dt_dev_get_pointer_zoom_pos(module->dev, x, y, &pzx, &pzy);
      gui->posx = (pzx + 0.5f)*module->dev->preview_pipe->backbuf_width;
      gui->posy = (pzy + 0.5f)*module->dev->preview_pipe->backbuf_height;
      gui->dx = gui->points[0] - gui->posx;
      gui->dy = gui->points[1] - gui->posy;
      return 1;
    }
    else if (gui->creation)
    {
      //we create the circle
      dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *) (malloc(sizeof(dt_masks_point_circle_t)));
      
      //we change the center value
      float pzx, pzy;
      dt_dev_get_pointer_zoom_pos(module->dev, x, y, &pzx, &pzy);
      pzx += 0.5f;
      pzy += 0.5f;
      float wd = module->dev->preview_pipe->backbuf_width;
      float ht = module->dev->preview_pipe->backbuf_height;
      float pts[2] = {pzx*wd,pzy*ht};
      dt_dev_distort_backtransform(module->dev,pts,1);
      circle->center[0] = pts[0]/module->dev->preview_pipe->iwidth;
      circle->center[1] = pts[1]/module->dev->preview_pipe->iheight;
      circle->radius = 0.1f;
      circle->border = 0.05f;
      form->points = g_list_append(form->points,circle);
      
      _gui_form_save_creation(module,form,gui);
      
      //we recreate the form points
      _gui_form_remove(module,form,gui);
      _gui_form_create(module,form,gui);
      
      //we save the move
      dt_dev_add_history_item(darktable.develop, module, TRUE);
      
      return 1;
    }
  }
  else if (form->type == DT_MASKS_BEZIER)
  {
    if (which == 1)
    {
      if (gui->creation)
      {
        dt_masks_point_bezier_t *bzpt = (dt_masks_point_bezier_t *) (malloc(sizeof(dt_masks_point_bezier_t)));
        int nb = g_list_length(form->points);
        //change the values
        float pzx, pzy;
        dt_dev_get_pointer_zoom_pos(module->dev, x, y, &pzx, &pzy);
        pzx += 0.5f;
        pzy += 0.5f;
        float wd = module->dev->preview_pipe->backbuf_width;
        float ht = module->dev->preview_pipe->backbuf_height;
        float pts[2] = {pzx*wd,pzy*ht};
        dt_dev_distort_backtransform(module->dev,pts,1);
        
        bzpt->corner[0] = pts[0]/module->dev->preview_pipe->iwidth;
        bzpt->corner[1] = pts[1]/module->dev->preview_pipe->iheight;
        bzpt->ctrl1[0] = bzpt->ctrl1[1] = bzpt->ctrl2[0] = bzpt->ctrl2[1] = -1.0;
        bzpt->state = DT_MASKS_POINT_STATE_NORMAL;
        
        //if that's the first point we should had another one as base point
        if (nb == 0)
        {
          dt_masks_point_bezier_t *bzpt2 = (dt_masks_point_bezier_t *) (malloc(sizeof(dt_masks_point_bezier_t)));
          bzpt2->corner[0] = pts[0]/module->dev->preview_pipe->iwidth;
          bzpt2->corner[1] = pts[1]/module->dev->preview_pipe->iheight;
          bzpt2->ctrl1[0] = bzpt2->ctrl1[1] = bzpt2->ctrl2[0] = bzpt2->ctrl2[1] = -1.0;
          bzpt2->state = DT_MASKS_POINT_STATE_NORMAL;
          form->points = g_list_append(form->points,bzpt2);
          nb++;
        }
        form->points = g_list_append(form->points,bzpt);
        
        gui->point_dragging = nb;
        
        _curve_init_ctrl_points(form);      
        
        //we recreate the form points
        gui->clockwise = _curve_is_clockwise(form);
        _gui_form_remove(module,form,gui);
        _gui_form_create(module,form,gui);
        
        dt_control_queue_redraw_center();
        return 1;
      }
      else if (gui->selected)
      {
        
      }
    }
    else if (which == 3)
    {
      if (gui->creation)
      {
        if (g_list_length(form->points) < 3)
        {
          //we remove the form
          dt_masks_free_form(form);
          module->dev->form_visible = NULL;
          dt_masks_init_formgui(module->dev);
          dt_control_queue_redraw_center();
          return 1;
        }
        else
        {
          //we delete last point (the one we are currently dragging)
          dt_masks_point_bezier_t *point = (dt_masks_point_bezier_t *)g_list_last(form->points)->data;
          form->points = g_list_remove(form->points,point);
          free(point);
          point = NULL;
          
          gui->point_dragging = -1;
          _curve_init_ctrl_points(form);
           gui->clockwise = _curve_is_clockwise(form);
          _gui_form_remove(module,form,gui);
          _gui_form_create(module,form,gui);
        
          //we save the form and quit creation mode
           _gui_form_save_creation(module,form,gui);
           dt_dev_add_history_item(darktable.develop, module, TRUE);
        }
      }
    }
  }
  
  return 0;
}
int dt_masks_scrolled (struct dt_iop_module_t *module, double x, double y, int up, uint32_t state)
{
  if (!module) return 0;
  dt_masks_form_t *form = module->dev->form_visible;
  dt_masks_form_gui_t *gui = module->dev->form_gui;
  
  if (form->type == DT_MASKS_CIRCLE)
  {
    if (gui->selected)
    {
      dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *) (g_list_first(form->points)->data);
      if (gui->border_selected)
      {
        if(up && circle->border > 0.002f) circle->border *= 0.9f;
        else  if(circle->border < 1.0f  ) circle->border *= 1.0f/0.9f;
        dt_masks_write_form(form,module->dev);
        _gui_form_update_border(module,form,gui);
      }
      else
      {
        if(up && circle->radius > 0.002f) circle->radius *= 0.9f;
        else  if(circle->radius < 1.0f  ) circle->radius *= 1.0f/0.9f;
        dt_masks_write_form(form,module->dev);
        _gui_form_remove(module,form,gui);
        _gui_form_create(module,form,gui);
      }
      dt_dev_add_history_item(darktable.develop, module, TRUE);
      return 1;
    }
  }
  return 0;
}
void dt_masks_post_expose (struct dt_iop_module_t *module, cairo_t *cr, int32_t width, int32_t height, int32_t pointerx, int32_t pointery)
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
  if (!_gui_form_test_create(module,form,gui)) return;
    
  //draw form
  if (form->type == DT_MASKS_CIRCLE) _circle_draw(cr,zoom_scale,gui);
  else if (form->type == DT_MASKS_BEZIER) _curve_draw(cr,zoom_scale,gui,g_list_length(form->points));
}

void dt_masks_set_inside(float x, int y, dt_masks_form_gui_t *gui)
{
  //we first check if it's inside borders
  int nb = 0;
  int last = -9999;
  for (int i=0; i<gui->border_count; i++)
  {
    int yy = (int) gui->border[i*2+1];
    if (yy != last && yy == y)
    {
      if (gui->border[i*2] > x) nb++;
    }
    last = yy;
  }  
  if (!(nb & 1))
  {
    gui->selected = FALSE;
    gui->border_selected = FALSE;
    return;
  }
  gui->selected = TRUE;
  
  //and we check if it's inside form
  nb = 0;
  last = -9999;
  for (int i=0; i<gui->points_count; i++)
  {
    int yy = (int) gui->points[i*2+1];
    if (yy != last && yy == y)
    {
      if (gui->points[i*2] > x) nb++;
    }
    last = yy;
  }
  gui->border_selected = !(nb & 1);
}

void dt_masks_init_formgui(dt_develop_t *dev)
{
  dev->form_gui->pipe_hash = dev->form_gui->formid = dev->form_gui->points_count = dev->form_gui->border_count = 0;
  if (dev->form_gui->points) free(dev->form_gui->points);
  dev->form_gui->points = NULL;
  if (dev->form_gui->border) free(dev->form_gui->border);
  dev->form_gui->border = NULL;
  dev->form_gui->posx = dev->form_gui->posy = dev->form_gui->dx = dev->form_gui->dy = 0.0f;
  dev->form_gui->selected = dev->form_gui->border_selected = dev->form_gui->form_dragging = FALSE;
  dev->form_gui->point_dragging = -1;
  dev->form_gui->creation = FALSE;
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
