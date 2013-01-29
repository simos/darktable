/*
    This file is part of darktable,
    copyright (c) 2011 henrik andersson.
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

static void _curve_get_XY(float p0x, float p0y, float p1x, float p1y, float p2x, float p2y, float p3x, float p3y,
                            float t, float *x, float *y)
{
  float a = (1-t)*(1-t)*(1-t);
  float b = 3*t*(1-t)*(1-t);
  float c = 3*t*t*(1-t);
  float d = t*t*t;
  *x =  p0x*a + p1x*b + p2x*c + p3x*d;
  *y =  p0y*a + p1y*b + p2y*c + p3y*d;
}

static void _curve_border_get_XY(float p0x, float p0y, float p1x, float p1y, float p2x, float p2y, float p3x, float p3y,
                                  float t, float rad, float *xc, float *yc, float *xb, float *yb)
{
  //we get the point
  _curve_get_XY(p0x,p0y,p1x,p1y,p2x,p2y,p3x,p3y,t,xc,yc);
  
  //now we get derivative points
  float a = 3*(1-t)*(1-t);
  float b = 3*((1-t)*(1-t) - 2*t*(1-t));
  float c = 3*(2*t*(1-t)-t*t);
  float d = 3*t*t;
  
  float dx = -p0x*a + p1x*b + p2x*c + p3x*d;
  float dy = -p0y*a + p1y*b + p2y*c + p3y*d;

  //so we can have the resulting point
  float l = 1.0/sqrtf(dx*dx+dy*dy);
  *xb = (*xc) + rad*dy*l;
  *yb = (*yc) - rad*dx*l;
}

//feather calculating (must be in "real" coordinate, to be sure everything is orthonormal)
static void _curve_ctrl2_to_feather(int ptx,int pty, int ctrlx, int ctrly, int *fx, int *fy, gboolean clockwise)
{
  if (clockwise)
  {
    *fx = ptx + ctrly - pty;
    *fy = pty + ptx - ctrlx;
  }
  else
  {
    *fx = ptx - ctrly + pty;
    *fy = pty - ptx + ctrlx;
  }
}

static void _curve_feather_to_ctrl(int ptx,int pty, int fx, int fy, int *ctrl1x, int *ctrl1y, int *ctrl2x, int *ctrl2y, gboolean clockwise)
{
  if (clockwise)
  {
    *ctrl2x = ptx + pty - fy;
    *ctrl2y = pty + fx - ptx;
    *ctrl1x = ptx - pty + fy;
    *ctrl1y = pty - fx + ptx;
  }
  else
  {
    *ctrl1x = ptx + pty - fy;
    *ctrl1y = pty + fx - ptx;
    *ctrl2x = ptx - pty + fy;
    *ctrl2y = pty - fx + ptx;
  }
}

//Get the control points of a segment to match exactly a catmull-rom spline
static void _curve_catmull_to_bezier(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
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
      _curve_catmull_to_bezier(point1->corner[0],point1->corner[1],
                        point2->corner[0],point2->corner[1],
                        point3->corner[0],point3->corner[1],
                        point4->corner[0],point4->corner[1],
                        &bx1,&by1,&bx2,&by2);
      if (point2->ctrl2[0] == -1.0) point2->ctrl2[0] = bx1;
      if (point2->ctrl2[1] == -1.0) point2->ctrl2[1] = by1;
      point3->ctrl1[0] = bx2;
      point3->ctrl1[1] = by2;
      _curve_catmull_to_bezier(point2->corner[0],point2->corner[1],
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
                                  double tmin, double tmax, float *curve_min, float *curve_max, float *border_min, float *border_max, 
                                  float *rcurve, float *rborder, float *curve, float *border, int *pos_curve, int *pos_border, int withborder)
{
  //we calcul points if needed
  if (curve_min[0] == -99999)
  {
    _curve_border_get_XY(p1[0],p1[1],p1[2],p1[3],p2[2],p2[3],p2[0],p2[1],tmin, p1[4]+(p2[4]-p1[4])*tmin,
                          curve_min,curve_min+1,border_min,border_min+1);
  }
  if (curve_max[0] == -99999)
  {
    _curve_border_get_XY(p1[0],p1[1],p1[2],p1[3],p2[2],p2[3],p2[0],p2[1],tmax, p1[4]+(p2[4]-p1[4])*tmax,
                          curve_max,curve_max+1,border_max,border_max+1);
  }
  //are the point near ? (we just test y as it's the value we use for rendering
  if ((tmax-tmin < 0.000001) || (curve_min[0]-curve_max[0]<=1 && curve_min[0]-curve_max[0]>=-1 &&
      curve_min[1]-curve_max[1]<=1 && curve_min[1]-curve_max[1]>=-1 &&
      (!withborder || (
      border_min[0]-border_max[0]<=1 && border_min[0]-border_max[0]>=-1 &&
      border_min[1]-border_max[1]<=1 && border_min[1]-border_max[1]>=-1))))
  {
    curve[*pos_curve] = curve_max[0];
    curve[*pos_curve+1] = curve_max[1];
    if (withborder) border[*pos_border] = border_max[0];
    if (withborder) border[*pos_border+1] = border_max[1];
    *pos_curve += 2;
    if (withborder) *pos_border += 2;
    rcurve[0] = curve_max[0];
    rcurve[1] = curve_max[1];
    if (withborder) rborder[0] = border_max[0];
    if (withborder) rborder[1] = border_max[1];;
    return;
  }
  
  //we split in two part
  double tx = (tmin+tmax)/2.0;
  float c[2] = {-99999,-99999}, b[2]= {-99999,-99999};
  float rc[2], rb[2];
  _curve_points_recurs(p1,p2,tmin,tx,curve_min,c,border_min,b,rc,rb,curve,border,pos_curve,pos_border,withborder);
  _curve_points_recurs(p1,p2,tx,tmax,rc,curve_max,rb,border_max,rcurve,rborder,curve,border,pos_curve,pos_border,withborder);
}

static int _curve_get_points_border(dt_develop_t *dev, dt_masks_form_t *form, int prio_max, dt_dev_pixelpipe_t *pipe, 
                                      float **points, int *points_count, float **border, int *border_count)
{
  float wd = pipe->iwidth, ht = pipe->iheight;
  
  //we allocate buffer (very large) => how to handle this ???
  *points = malloc(60000*sizeof(float));
  *border = malloc(60000*sizeof(float));
  
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
  //for the border, we store value too
  for(int k = 0; k < nb; k++)
  {
    (*border)[k*6] = 0.0; //x position of the border point
    (*border)[k*6+1] = 0.0; //y position of the border point
    (*border)[k*6+2] = 0.0; //start index for the initial gap. if <0 this mean we have to skip to index (-x)
    (*border)[k*6+3] = 0.0; //end index for the initial gap
    (*border)[k*6+4] = 0.0; //start index for the final gap. if <0 this mean we have to stop at index (-x)
    (*border)[k*6+5] = 0.0; //end index for the final gap
  }
  
  int pos = 6*nb;
  int posb = 6*nb;
  int *pos_curve = malloc(sizeof(int)*nb);
  int *pos_border = malloc(sizeof(int)*nb);
  float *border_init = malloc(sizeof(float)*6*nb);
  int cw = _curve_is_clockwise(form);
  if (cw == 0) cw = -1;
  //we render all segments
  for(int k = 0; k < nb; k++)
  {
    pos_curve[k] = pos;
    pos_border[k] = posb;
    border_init[k*6+2] = -posb;
    int k2 = (k+1)%nb;
    dt_masks_point_bezier_t *point1 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k);
    dt_masks_point_bezier_t *point2 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k2);
    float p1[5] = {point1->corner[0]*wd, point1->corner[1]*ht, point1->ctrl2[0]*wd, point1->ctrl2[1]*ht, cw*point1->border[1]*MIN(wd,ht)};
    float p2[5] = {point2->corner[0]*wd, point2->corner[1]*ht, point2->ctrl1[0]*wd, point2->ctrl1[1]*ht, cw*point2->border[0]*MIN(wd,ht)};
    
    //we store the first point
    //(*points)[pos++] = p1[0];
    //(*points)[pos++] = p1[1];
    
    //and we determine all points by recursion (to be sure the distance between 2 points is <=1)
    float rc[2],rb[2];
    float bmin[2] = {-99999,-99999};
    float bmax[2] = {-99999,-99999};
    float cmin[2] = {-99999,-99999};
    float cmax[2] = {-99999,-99999};
    _curve_points_recurs(p1,p2,0.0,1.0,cmin,cmax,bmin,bmax,rc,rb,*points,*border,&pos,&posb,(nb>=3));
    (*points)[pos++] = rc[0];
    (*points)[pos++] = rc[1];
    (*border)[posb++] = rb[0];
    (*border)[posb++] = rb[1];
    border_init[k*6+4] = -posb;
    border_init[k*6] = (*border)[pos_border[k]];
    border_init[k*6+1] = (*border)[pos_border[k]+1];
  }
  *points_count = pos/2;
  
  //now we have do do some adujstements for the border (self-intersecting and gap due to sharp corner)
  for(int k = 0; k < nb; k++)
  {
    //is the corner convex ?
    float p1x,p1y,p2x,p2y;
    if (k == 0)
    {
      p1x = (*points)[pos-6];
      p1y = (*points)[pos-5];
    }
    else
    {
      p1x = (*points)[pos_curve[k]-6];
      p1y = (*points)[pos_curve[k]-5];
    }
    p2x = (*points)[pos_curve[k]+6];
    p2y = (*points)[pos_curve[k]+7];
    float pdv = ((*points)[k*6+2]-p1x)*(p2y-(*points)[k*6+3]) - ((*points)[k*6+3]-p1y)*(p2x-(*points)[k*6+2]);
    if (pdv > 0)
    {
      //we have to be sure there is no gap
    }
    else
    {
      //we have to get ride of self intersection
      //we want to find the intersection
      int pos0,pos1,pos2,pos3 ;
      if (k==0)
      {
        pos0 = pos_border[nb-1];
        pos1 = pos;
        pos2 = pos_border[0];
        pos3 = pos_border[1];
      }
      else if (k == nb-1)
      {
        pos0 = pos_border[k-1];
        pos1 = pos2 = pos_border[k];
        pos3 = pos;
      }
      else
      {
        pos0 = pos_border[k-1];
        pos1 = pos2 = pos_border[k];
        pos3 = pos_border[k+1];
      }
      int inter0 = 0, inter1 = 0;
      for (int i=pos0 ; i<pos1 ; i+=2)
      {
        for (int j=pos3-2 ; j>pos2 ; j-=2)
        {
          if ((*border)[i]-(*border)[j]<=1 && (*border)[i]-(*border)[j]>=-1 &&
              (*border)[i+1]-(*border)[j+1]<=1 && (*border)[i+1]-(*border)[j+1]>=-1)
          {
            inter1 = j;
            break;
          }
        }
        if (inter1 > 0)
        {
          inter0 = i;
          break;
        }
      }
      if (inter0 > 0 && inter1 > 0)
      {
        border_init[k*6+2] = -inter1;
        if (k==0) border_init[(nb-1)*6+4] = -inter0;
        else border_init[(k-1)*6+4] = -inter0;
      }
    }
  }
  
  *border_count = posb/2;
  free(pos_curve);
  free(pos_border);  
  
  //and we transform them with all distorted modules
  if (dt_dev_distort_transform_plus(dev,pipe,0,prio_max,*points,*points_count) && dt_dev_distort_transform_plus(dev,pipe,0,prio_max,*border,*border_count))
  {
    memcpy(*border,border_init,sizeof(float)*6*nb);
    free(border_init);
    return 1;
  }
  
  //if we failed, then free all and return
  free(*points);
  *points = NULL;
  *points_count = 0;
  free(*border);
  *border = NULL;
  *border_count = 0;
  return 0; 
}

int dt_curve_get_points_border(dt_develop_t *dev, dt_masks_form_t *form, float **points, int *points_count, float **border, int *border_count)
{
  return _curve_get_points_border(dev,form,999999,dev->preview_pipe,points,points_count,border,border_count);
}

int dt_curve_get_border(dt_develop_t *dev, dt_masks_form_t *form, float **points, int *points_count)
{
  *points_count = 0;
  return 1;
}

int dt_curve_events_mouse_scrolled(struct dt_iop_module_t *module, float pzx, float pzy, int up, uint32_t state,
                                          dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  if (gui->form_selected)
  {
    float amount = 1.05;
    if (!up) amount = 0.95;
    int nb = g_list_length(form->points);
    if (gui->border_selected)
    {
      for(int k = 0; k < nb; k++)
      {
        dt_masks_point_bezier_t *point = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k);
        point->border[0] *= amount;
        point->border[1] *= amount;
      }
    }
    else
    {
      //get the center of gravity of the form (like if it was a simple polygon)
      float bx = 0.0f;
      float by = 0.0f;
      float surf = 0.0f;
      
      for(int k = 0; k < nb; k++)
      {
        int k2 = (k+1)%nb;
        dt_masks_point_bezier_t *point1 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k);
        dt_masks_point_bezier_t *point2 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k2);
        surf += point1->corner[0]*point2->corner[1] - point2->corner[0]*point1->corner[1];
        
        bx += (point1->corner[0] + point2->corner[0])*(point1->corner[0]*point2->corner[1] - point2->corner[0]*point1->corner[1]);
        by += (point1->corner[1] + point2->corner[1])*(point1->corner[0]*point2->corner[1] - point2->corner[0]*point1->corner[1]);
      }
      bx /= 3.0*surf;
      by /= 3.0*surf;
      
      //first, we have to be sure that the shape is not too small to be resized
      if (amount < 1.0)
      {
        for(int k = 0; k < nb; k++)
        {
          dt_masks_point_bezier_t *point = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k);
          float l = (point->corner[0]-bx)*(point->corner[0]-bx) + (point->corner[1]-by)*(point->corner[1]-by);
          if ( l < 0.0005f) return 1;
        }
      }
      //now we move each point
      for(int k = 0; k < nb; k++)
      {
        dt_masks_point_bezier_t *point = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k);
        float x = (point->corner[0]-bx)*amount;
        float y = (point->corner[1]-by)*amount;
        
        //we stretch ctrl points
        float ct1x = (point->ctrl1[0]-point->corner[0])*amount;
        float ct1y = (point->ctrl1[1]-point->corner[1])*amount;
        float ct2x = (point->ctrl2[0]-point->corner[0])*amount;
        float ct2y = (point->ctrl2[1]-point->corner[1])*amount;
        
        //and we set the new points
        point->corner[0] = bx + x;
        point->corner[1] = by + y;
        point->ctrl1[0] = point->corner[0] + ct1x;
        point->ctrl1[1] = point->corner[1] + ct1y;
        point->ctrl2[0] = point->corner[0] + ct2x;
        point->ctrl2[1] = point->corner[1] + ct2y;   
      }
      
      //now the redraw/save stuff
      _curve_init_ctrl_points(form);
    }
  
    dt_masks_write_form(form,module->dev);

    //we recreate the form points
    dt_masks_gui_form_remove(module,form,gui);
    dt_masks_gui_form_create(module,form,gui);
    
    //we save the move
    dt_dev_add_history_item(darktable.develop, module, TRUE);
    return 1;
  }
  return 0;
}

int dt_curve_events_button_pressed(struct dt_iop_module_t *module,float pzx, float pzy, int which, int type, uint32_t state,
                                          dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  if (which == 3 || (gui->creation && gui->creation_closing_form))
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
        dt_masks_gui_form_remove(module,form,gui);
        dt_masks_gui_form_create(module,form,gui);
      
        //we save the form and quit creation mode
         dt_masks_gui_form_save_creation(module,form,gui);
         dt_dev_add_history_item(darktable.develop, module, TRUE);
      }
    }
  }
  else if (which == 1)
  {
    if (gui->creation)
    {
      dt_masks_point_bezier_t *bzpt = (dt_masks_point_bezier_t *) (malloc(sizeof(dt_masks_point_bezier_t)));
      int nb = g_list_length(form->points);
      //change the values
      float wd = module->dev->preview_pipe->backbuf_width;
      float ht = module->dev->preview_pipe->backbuf_height;
      float pts[2] = {pzx*wd,pzy*ht};
      dt_dev_distort_backtransform(module->dev,pts,1);
      
      bzpt->corner[0] = pts[0]/module->dev->preview_pipe->iwidth;
      bzpt->corner[1] = pts[1]/module->dev->preview_pipe->iheight;
      bzpt->ctrl1[0] = bzpt->ctrl1[1] = bzpt->ctrl2[0] = bzpt->ctrl2[1] = -1.0;
      bzpt->border[0] = bzpt->border[1] = 0.05;
      bzpt->state = DT_MASKS_POINT_STATE_NORMAL;
      
      //if that's the first point we should had another one as base point
      if (nb == 0)
      {
        dt_masks_point_bezier_t *bzpt2 = (dt_masks_point_bezier_t *) (malloc(sizeof(dt_masks_point_bezier_t)));
        bzpt2->corner[0] = pts[0]/module->dev->preview_pipe->iwidth;
        bzpt2->corner[1] = pts[1]/module->dev->preview_pipe->iheight;
        bzpt2->ctrl1[0] = bzpt2->ctrl1[1] = bzpt2->ctrl2[0] = bzpt2->ctrl2[1] = -1.0;
        bzpt2->border[0] = bzpt2->border[1] = 0.05;
        bzpt2->state = DT_MASKS_POINT_STATE_NORMAL;
        form->points = g_list_append(form->points,bzpt2);
        nb++;
      }
      form->points = g_list_append(form->points,bzpt);
      
      gui->point_dragging = nb;
      
      _curve_init_ctrl_points(form);      
      
      //we recreate the form points
      gui->clockwise = _curve_is_clockwise(form);
      dt_masks_gui_form_remove(module,form,gui);
      dt_masks_gui_form_create(module,form,gui);
      
      dt_control_queue_redraw_center();
      return 1;
    }
    else if (gui->form_selected)
    {
      gui->form_dragging = TRUE;
      gui->posx = pzx*module->dev->preview_pipe->backbuf_width;
      gui->posy = pzy*module->dev->preview_pipe->backbuf_height;
      gui->dx = gui->points[2] - gui->posx;
      gui->dy = gui->points[3] - gui->posy;
      return 1;
    }
    else if (gui->point_selected >= 0)
    {
      gui->point_dragging  = gui->point_selected;
      dt_control_queue_redraw_center();
      return 1;
    }
    else if (gui->feather_selected >= 0)
    {
      gui->feather_dragging = gui->feather_selected;
      dt_control_queue_redraw_center();
      return 1;
    }
    else if (gui->point_border_selected >= 0)
    {
      gui->point_border_dragging = gui->point_border_selected;
      dt_control_queue_redraw_center();
      return 1;
    }
    else if (gui->seg_selected >= 0)
    {
      if ((state&GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
      {
        //we add a new point to the curve
        dt_masks_point_bezier_t *bzpt = (dt_masks_point_bezier_t *) (malloc(sizeof(dt_masks_point_bezier_t)));
        //change the values
        float wd = module->dev->preview_pipe->backbuf_width;
        float ht = module->dev->preview_pipe->backbuf_height;
        float pts[2] = {pzx*wd,pzy*ht};
        dt_dev_distort_backtransform(module->dev,pts,1);
        
        bzpt->corner[0] = pts[0]/module->dev->preview_pipe->iwidth;
        bzpt->corner[1] = pts[1]/module->dev->preview_pipe->iheight;
        bzpt->ctrl1[0] = bzpt->ctrl1[1] = bzpt->ctrl2[0] = bzpt->ctrl2[1] = -1.0;
        bzpt->state = DT_MASKS_POINT_STATE_NORMAL;
        bzpt->border[0] = bzpt->border[1] = 0.05;
        form->points = g_list_insert(form->points,bzpt,gui->seg_selected+1);
        _curve_init_ctrl_points(form);
        gui->point_dragging  = gui->point_selected = gui->seg_selected+1;
        gui->seg_selected = -1;
        dt_control_queue_redraw_center();
      }
      else
      {
        //we move the entire segment
        gui->seg_dragging = gui->seg_selected;
        gui->posx = pzx*module->dev->preview_pipe->backbuf_width;
        gui->posy = pzy*module->dev->preview_pipe->backbuf_height;
        gui->dx = gui->points[gui->seg_selected*6+2] - gui->posx;
        gui->dy = gui->points[gui->seg_selected*6+3] - gui->posy;
      }
      return 1;
    }
  }
  return 0;
}

int dt_curve_events_button_released(struct dt_iop_module_t *module,float pzx, float pzy, int which, uint32_t state,
                                          dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  if (gui->creation) return 1;
  
  if (gui->form_dragging)
  {
    //we end the form dragging
    gui->form_dragging = FALSE;
    
    //we get point0 new values
    dt_masks_point_bezier_t *point = (dt_masks_point_bezier_t *)g_list_first(form->points)->data;
    float wd = module->dev->preview_pipe->backbuf_width;
    float ht = module->dev->preview_pipe->backbuf_height;
    float pts[2] = {pzx*wd+gui->dx,pzy*ht+gui->dy};
    dt_dev_distort_backtransform(module->dev,pts,1);
    float dx = pts[0]/module->dev->preview_pipe->iwidth - point->corner[0];
    float dy = pts[1]/module->dev->preview_pipe->iheight - point->corner[1];
    
    //we move all points
    GList *points = g_list_first(form->points);
    while (points)
    {
      point = (dt_masks_point_bezier_t *)points->data;
      point->corner[0] += dx;
      point->corner[1] += dy;
      point->ctrl1[0] += dx;
      point->ctrl1[1] += dy;
      point->ctrl2[0] += dx;
      point->ctrl2[1] += dy;      
      points = g_list_next(points);
    }
    
    dt_masks_write_form(form,module->dev);

    //we recreate the form points
    dt_masks_gui_form_remove(module,form,gui);
    dt_masks_gui_form_create(module,form,gui);
    
    //we save the move
    dt_dev_add_history_item(darktable.develop, module, TRUE);
    
    return 1;
  }
  else if (gui->seg_dragging>=0)
  {
    gui->seg_dragging = -1;
    dt_dev_add_history_item(darktable.develop, module, TRUE);
    return 1;
  }
  else if (gui->point_dragging >= 0)
  {
    dt_masks_point_bezier_t *point = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,gui->point_dragging);
    gui->point_dragging = -1;
    float wd = module->dev->preview_pipe->backbuf_width;
    float ht = module->dev->preview_pipe->backbuf_height;
    float pts[2] = {pzx*wd,pzy*ht};
    dt_dev_distort_backtransform(module->dev,pts,1);
    float dx = pts[0]/module->dev->preview_pipe->iwidth - point->corner[0];
    float dy = pts[1]/module->dev->preview_pipe->iheight - point->corner[1];    
    
    point->corner[0] += dx;
    point->corner[1] += dy;
    point->ctrl1[0] += dx;
    point->ctrl1[1] += dy;
    point->ctrl2[0] += dx;
    point->ctrl2[1] += dy;
    
    _curve_init_ctrl_points(form);
    
    dt_masks_write_form(form,module->dev);

    //we recreate the form points
    dt_masks_gui_form_remove(module,form,gui);
    dt_masks_gui_form_create(module,form,gui);
    
    //we save the move
    dt_dev_add_history_item(darktable.develop, module, TRUE);
    
    return 1;
  }
  else if (gui->feather_dragging >= 0)
  {
    dt_masks_point_bezier_t *point = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,gui->feather_dragging);
    gui->feather_dragging = -1;
    float wd = module->dev->preview_pipe->backbuf_width;
    float ht = module->dev->preview_pipe->backbuf_height;
    float pts[2] = {pzx*wd,pzy*ht};
    dt_dev_distort_backtransform(module->dev,pts,1);   
    
    int p1x,p1y,p2x,p2y;
    _curve_feather_to_ctrl(point->corner[0]*module->dev->preview_pipe->iwidth,point->corner[1]*module->dev->preview_pipe->iheight,pts[0],pts[1],
                            &p1x,&p1y,&p2x,&p2y,gui->clockwise);
    point->ctrl1[0] = (float)p1x/module->dev->preview_pipe->iwidth;
    point->ctrl1[1] = (float)p1y/module->dev->preview_pipe->iheight;
    point->ctrl2[0] = (float)p2x/module->dev->preview_pipe->iwidth;
    point->ctrl2[1] = (float)p2y/module->dev->preview_pipe->iheight;
    
    point->state = DT_MASKS_POINT_STATE_USER;
    
    _curve_init_ctrl_points(form);
    
    dt_masks_write_form(form,module->dev);

    //we recreate the form points
    dt_masks_gui_form_remove(module,form,gui);
    dt_masks_gui_form_create(module,form,gui);
    
    //we save the move
    dt_dev_add_history_item(darktable.develop, module, TRUE);
    
    return 1;
  }
  else if (gui->point_border_dragging >= 0)
  {
    gui->point_border_dragging = -1;
    //we save the move
    dt_dev_add_history_item(darktable.develop, module, TRUE);
    dt_control_queue_redraw_center();
    return 1;
  }
  else if (gui->point_selected>=0 && which == 3)
  {
    //we remove the point (and the entire form if there is too few points)
    if (g_list_length(form->points) < 4)
    {
      //we remove the form
      dt_masks_free_form(form);
      module->dev->form_visible = NULL;
      dt_masks_init_formgui(module->dev);
      int pos = 0;
      for (int i=0; i<module->blend_params->forms_count; i++)
      {
        if (module->blend_params->forms[i] == form->formid)
        {
          pos = i;
          break;
        }
      }
      module->blend_params->forms_count--;
      for (int i=pos; i<module->blend_params->forms_count; i++) module->blend_params->forms[i] = module->blend_params->forms[i+1];
      dt_iop_gui_blend_data_t *bd = (dt_iop_gui_blend_data_t *)module->blend_data;
      gtk_widget_destroy(bd->form_label[pos]);
      for (int i=pos; i<module->blend_params->forms_count; i++) bd->form_label[i] = bd->form_label[i+1];
  
      dt_control_queue_redraw_center();
      dt_dev_add_history_item(darktable.develop, module, TRUE);
      return 1;
    }
    form->points = g_list_delete_link(form->points,g_list_nth(form->points,gui->point_selected));
    gui->point_selected = -1;
    _curve_init_ctrl_points(form);
    
    dt_masks_write_form(form,module->dev);

    //we recreate the form points
    dt_masks_gui_form_remove(module,form,gui);
    dt_masks_gui_form_create(module,form,gui);
    
    //we save the move
    dt_dev_add_history_item(darktable.develop, module, TRUE);
    
    return 1;
  }
  else if (gui->feather_selected>=0 && which == 3)
  {
    dt_masks_point_bezier_t *point = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,gui->feather_selected);
    if (point->state != DT_MASKS_POINT_STATE_NORMAL)
    {
      point->state = DT_MASKS_POINT_STATE_NORMAL;
      _curve_init_ctrl_points(form);
      
      dt_masks_write_form(form,module->dev);
  
      //we recreate the form points
      dt_masks_gui_form_remove(module,form,gui);
      dt_masks_gui_form_create(module,form,gui);
      
      //we save the move
      dt_dev_add_history_item(darktable.develop, module, TRUE);
    }
    return 1;
  }
  
  return 0;
}

int dt_curve_events_mouse_moved(struct dt_iop_module_t *module,float pzx, float pzy, int which, dt_masks_form_t *form, dt_masks_form_gui_t *gui)
{
  int32_t zoom, closeup;
  DT_CTL_GET_GLOBAL(zoom, dev_zoom);
  DT_CTL_GET_GLOBAL(closeup, dev_closeup);
  float zoom_scale = dt_dev_get_zoom_scale(module->dev, zoom, closeup ? 2 : 1, 1);
  float as = 0.005f/zoom_scale*module->dev->preview_pipe->backbuf_width;
  
  if (gui->point_dragging >=0)
  {
    float wd = module->dev->preview_pipe->backbuf_width;
    float ht = module->dev->preview_pipe->backbuf_height;
    float pts[2] = {pzx*wd,pzy*ht};
    if (gui->creation && g_list_length(form->points)>3)
    {
      //if we are near the first point, we have to say that the form should be closed
      if (pts[0]-gui->points[2] < as && pts[0]-gui->points[2] > -as && pts[1]-gui->points[3] < as && pts[1]-gui->points[3] > -as)
      {
        gui->creation_closing_form = TRUE;
      }
      else
      {
        gui->creation_closing_form = FALSE;
      }
    }
    
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
    dt_masks_gui_form_remove(module,form,gui);
    dt_masks_gui_form_create(module,form,gui);
    dt_control_queue_redraw_center();
    return 1;
  }
  else if (gui->seg_dragging >= 0)
  {
    //we get point0 new values
    int pos2 = (gui->seg_dragging+1)%g_list_length(form->points);
    dt_masks_point_bezier_t *point = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,gui->seg_dragging);
    dt_masks_point_bezier_t *point2 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,pos2);
    float wd = module->dev->preview_pipe->backbuf_width;
    float ht = module->dev->preview_pipe->backbuf_height;
    float pts[2] = {pzx*wd+gui->dx,pzy*ht+gui->dy};
    dt_dev_distort_backtransform(module->dev,pts,1);
    float dx = pts[0]/module->dev->preview_pipe->iwidth - point->corner[0];
    float dy = pts[1]/module->dev->preview_pipe->iheight - point->corner[1];
    
    //we move all points
    point->corner[0] += dx;
    point->corner[1] += dy;
    point->ctrl1[0] += dx;
    point->ctrl1[1] += dy;
    point->ctrl2[0] += dx;
    point->ctrl2[1] += dy;
    point2->corner[0] += dx;
    point2->corner[1] += dy;
    point2->ctrl1[0] += dx;
    point2->ctrl1[1] += dy;
    point2->ctrl2[0] += dx;
    point2->ctrl2[1] += dy;
    
    _curve_init_ctrl_points(form);
    
    dt_masks_write_form(form,module->dev);

    //we recreate the form points
    dt_masks_gui_form_remove(module,form,gui);
    dt_masks_gui_form_create(module,form,gui);
    
    dt_control_queue_redraw_center();
    return 1;
  }
  else if (gui->feather_dragging >= 0)
  {
    float wd = module->dev->preview_pipe->backbuf_width;
    float ht = module->dev->preview_pipe->backbuf_height;
    float pts[2] = {pzx*wd,pzy*ht};
    dt_dev_distort_backtransform(module->dev,pts,1);
    dt_masks_point_bezier_t *point = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,gui->feather_dragging);
    
    int p1x,p1y,p2x,p2y;
    _curve_feather_to_ctrl(point->corner[0]*module->dev->preview_pipe->iwidth,point->corner[1]*module->dev->preview_pipe->iheight,pts[0],pts[1],
                            &p1x,&p1y,&p2x,&p2y,gui->clockwise);
    point->ctrl1[0] = (float)p1x/module->dev->preview_pipe->iwidth;
    point->ctrl1[1] = (float)p1y/module->dev->preview_pipe->iheight;
    point->ctrl2[0] = (float)p2x/module->dev->preview_pipe->iwidth;
    point->ctrl2[1] = (float)p2y/module->dev->preview_pipe->iheight;
    point->state = DT_MASKS_POINT_STATE_USER;
    
    _curve_init_ctrl_points(form);
    //we recreate the form points
    dt_masks_gui_form_remove(module,form,gui);
    dt_masks_gui_form_create(module,form,gui);
    dt_control_queue_redraw_center();
    return 1;
  }
  else if (gui->point_border_dragging >= 0)
  {
    float wd = module->dev->preview_pipe->backbuf_width;
    float ht = module->dev->preview_pipe->backbuf_height;
    
    int k = gui->point_border_dragging;
    
    //now we want to know the position reflected on actual corner/border segment
    float a = (gui->border[k*6+1]-gui->points[k*6+3])/(float)(gui->border[k*6]-gui->points[k*6+2]);
    float b = gui->points[k*6+3]-a*gui->points[k*6+2];
    
    float pts[2];
    pts[0] = (a*pzy*ht+pzx*wd-b*a)/(a*a+1.0);
    pts[1] = a*pts[0]+b;
    
    dt_dev_distort_backtransform(module->dev,pts,1);
    
    dt_masks_point_bezier_t *point = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k);
    float nx = point->corner[0]*module->dev->preview_pipe->iwidth;
    float ny = point->corner[1]*module->dev->preview_pipe->iheight;
    float nr = sqrtf((pts[0]-nx)*(pts[0]-nx) + (pts[1]-ny)*(pts[1]-ny));
    
    point->border[0] = point->border[1] = nr/fminf(module->dev->preview_pipe->iwidth,module->dev->preview_pipe->iheight);
    
    //we recreate the form points
    dt_masks_gui_form_remove(module,form,gui);
    dt_masks_gui_form_create(module,form,gui);
    dt_control_queue_redraw_center();
    return 1;
  }
  else if (gui->form_dragging)
  {
    gui->posx = pzx*module->dev->preview_pipe->backbuf_width;
    gui->posy = pzy*module->dev->preview_pipe->backbuf_height;
    dt_control_queue_redraw_center();
    return 1;
  }
  
  gui->form_selected = FALSE;
  gui->border_selected = FALSE;
  gui->feather_selected  = -1;
  gui->point_selected = -1;
  gui->seg_selected = -1;
  gui->point_border_selected = -1;
  //are we near a point or feather ?
  int nb = g_list_length(form->points);

  pzx *= module->dev->preview_pipe->backbuf_width, pzy *= module->dev->preview_pipe->backbuf_height;
  gui->feather_selected = -1;
  gui->point_selected = -1;
  for (int k=0;k<nb;k++)
  {
    //feather ???
    int ffx,ffy;
    _curve_ctrl2_to_feather(gui->points[k*6+2],gui->points[k*6+3],gui->points[k*6+4],gui->points[k*6+5],&ffx,&ffy,gui->clockwise);
    if (pzx-ffx>-as && pzx-ffx<as && pzy-ffy>-as && pzy-ffy<as)
    {
      gui->feather_selected = k;
      dt_control_queue_redraw_center();
      return 1;
    }
    
    //corner ??
    if (pzx-gui->points[k*6+2]>-as && pzx-gui->points[k*6+2]<as && pzy-gui->points[k*6+3]>-as && pzy-gui->points[k*6+3]<as)
    {
      gui->point_selected = k;
      dt_control_queue_redraw_center();
      return 1;
    }
    
    //border corner ??
    if (pzx-gui->border[k*6]>-as && pzx-gui->border[k*6]<as && pzy-gui->border[k*6+1]>-as && pzy-gui->border[k*6+1]<as)
    {
      gui->point_border_selected = k;
      dt_control_queue_redraw_center();
      return 1;
    }
  }
  
  //are we inside the form or the borders or near a segment ???
  int in, inb, near;
  dt_masks_get_distance(pzx,(int)pzy,as,gui,form,&in,&inb,&near);
  gui->seg_selected = near;
  if (near<0)
  {
    if (inb)
    {
      gui->form_selected = TRUE;
      gui->border_selected = TRUE;
    }
    else if (in)
    {
      gui->form_selected = TRUE;
    }
  }
  dt_control_queue_redraw_center();
  if (!gui->form_selected && !gui->border_selected && gui->seg_selected<0) return 0;
  return 1;
}

void dt_curve_events_post_expose(cairo_t *cr, float zoom_scale, dt_masks_form_gui_t *gui, int nb)
{
  double dashed[] = {4.0, 4.0};
  dashed[0] /= zoom_scale;
  dashed[1] /= zoom_scale;
  int len  = sizeof(dashed) / sizeof(dashed[0]);
  float dx=0, dy=0;
  if (gui->form_dragging)
  {
    dx = gui->posx + gui->dx - gui->points[2];
    dy = gui->posy + gui->dy - gui->points[3];
  }
  
  //draw curve
  if (gui->points_count > nb*3+6)
  { 
    cairo_set_dash(cr, dashed, 0, 0);
    
    cairo_move_to(cr,gui->points[nb*6]+dx,gui->points[nb*6+1]+dy);
    int seg = 1, seg2 = 0;
    for (int i=nb*3; i<gui->points_count; i++)
    {
      //we decide to hightlight the form segment by segment
      if (gui->points[i*2+1] == gui->points[seg*6+3] && gui->points[i*2] == gui->points[seg*6+2])
      {
        //this is the end of the last segment, so we have to draw it
        if(gui->form_selected || gui->form_dragging || gui->seg_selected==seg2) cairo_set_line_width(cr, 5.0/zoom_scale);
        else                                     cairo_set_line_width(cr, 3.0/zoom_scale);
        cairo_set_source_rgba(cr, .3, .3, .3, .8);
        cairo_stroke_preserve(cr);
        if(gui->form_selected || gui->form_dragging || gui->seg_selected==seg2) cairo_set_line_width(cr, 2.0/zoom_scale);
        else                                     cairo_set_line_width(cr, 1.0/zoom_scale);
        cairo_set_source_rgba(cr, .8, .8, .8, .8);
        cairo_stroke(cr);
        //and we update the segment number
        seg = (seg+1)%nb;
        seg2++;
      }
      cairo_line_to(cr,gui->points[i*2]+dx,gui->points[i*2+1]+dy);
    }
  }
  
  //draw corners
  float anchor_size;
  for(int k = 0; k < nb; k++)
  {
    if (k == gui->point_dragging || k == gui->point_selected)
    {
      anchor_size = 7.0f / zoom_scale;
    }
    else
    {
      anchor_size = 5.0f / zoom_scale;
    }
    cairo_set_source_rgba(cr, .8, .8, .8, .8);
    cairo_rectangle(cr, 
        gui->points[k*6+2] - (anchor_size*0.5)+dx, 
        gui->points[k*6+3] - (anchor_size*0.5)+dy, 
        anchor_size, anchor_size);
    cairo_fill_preserve(cr);

    if (k == gui->point_dragging || k == gui->point_selected ) cairo_set_line_width(cr, 2.0/zoom_scale);
    else if ((k == 0 || k == nb) && gui->creation && gui->creation_closing_form) cairo_set_line_width(cr, 2.0/zoom_scale);
    else cairo_set_line_width(cr, 1.0/zoom_scale);
    cairo_set_source_rgba(cr, .3, .3, .3, .8);
    cairo_stroke(cr);
  }
  
  //draw feathers
  for(int k = 0; k < nb; k++)
  {
    //uncomment this part if you want to see "real" control points
    /*cairo_move_to(cr, gui->points[k*6+2]+dx,gui->points[k*6+3]+dy);
    cairo_line_to(cr, gui->points[k*6]+dx,gui->points[k*6+1]+dy);
    cairo_stroke(cr);
    cairo_move_to(cr, gui->points[k*6+2]+dx,gui->points[k*6+3]+dy);
    cairo_line_to(cr, gui->points[k*6+4]+dx,gui->points[k*6+5]+dy);
    cairo_stroke(cr);*/
    int ffx,ffy;
    _curve_ctrl2_to_feather(gui->points[k*6+2]+dx,gui->points[k*6+3]+dy,gui->points[k*6+4]+dx,gui->points[k*6+5]+dy,&ffx,&ffy,gui->clockwise);
    cairo_move_to(cr, gui->points[k*6+2]+dx,gui->points[k*6+3]+dy);
    cairo_line_to(cr,ffx,ffy);
    cairo_set_line_width(cr, 1.5/zoom_scale);
    cairo_set_source_rgba(cr, .3, .3, .3, .8);
    cairo_stroke_preserve(cr);
    cairo_set_line_width(cr, 0.75/zoom_scale);
    cairo_set_source_rgba(cr, .8, .8, .8, .8);
    cairo_stroke(cr);
    
    if (k == gui->feather_dragging || k == gui->feather_selected) cairo_arc (cr, ffx,ffy, 3.0f / zoom_scale, 0, 2.0*M_PI);
    else cairo_arc (cr, ffx,ffy, 1.5f / zoom_scale, 0, 2.0*M_PI);
    cairo_set_source_rgba(cr, .8, .8, .8, .8);
    cairo_fill_preserve(cr);

    cairo_set_line_width(cr, 1.0/zoom_scale);
    cairo_set_source_rgba(cr, .3, .3, .3, .8);
    cairo_stroke(cr);
  }
      
  //draw border and corners
  if (gui->border_count > nb*3+6)
  { 
    cairo_move_to(cr,gui->border[0]+dx,gui->border[1]+dy);
    //we draw the curve segment by segment
    for (int k=0; k<nb; k++)
    {
      
      int pos1 = -gui->border[k*6+2];
      int pos2 = -gui->border[k*6+4];
      
      if (pos1<0)
      {
        //we have to draw the initial gap
      }
      //we draw the segment
      cairo_move_to(cr,gui->border[pos1]+dx,gui->border[pos1+1]+dy);
      for (int i=pos1; i<pos2; i+=2)
      {
        cairo_line_to(cr,gui->border[i]+dx,gui->border[i+1]+dy);
      }      
      if (pos2<0)
      {
        //we have to draw the end gap
      }
      
      //the execute the drawing
      if(gui->border_selected) cairo_set_line_width(cr, 2.0/zoom_scale);
      else                                     cairo_set_line_width(cr, 1.0/zoom_scale);
      cairo_set_source_rgba(cr, .3, .3, .3, .8);
      cairo_set_dash(cr, dashed, len, 0);
      cairo_stroke_preserve(cr);
      if(gui->border_selected) cairo_set_line_width(cr, 2.0/zoom_scale);
      else                                     cairo_set_line_width(cr, 1.0/zoom_scale);
      cairo_set_source_rgba(cr, .8, .8, .8, .8);
      cairo_set_dash(cr, dashed, len, 4);
      cairo_stroke(cr);
      
      //draw the point
      if (gui->point_border_selected == k)
      {
        anchor_size = 7.0f / zoom_scale;
      }
      else
      {
        anchor_size = 5.0f / zoom_scale;
      }
      cairo_set_source_rgba(cr, .8, .8, .8, .8);
      cairo_rectangle(cr, 
          gui->border[k*6] - (anchor_size*0.5)+dx, 
          gui->border[k*6+1] - (anchor_size*0.5)+dy, 
          anchor_size, anchor_size);
      cairo_fill_preserve(cr);
  
      if (gui->point_border_selected == k) cairo_set_line_width(cr, 2.0/zoom_scale);
      else cairo_set_line_width(cr, 1.0/zoom_scale);
      cairo_set_source_rgba(cr, .3, .3, .3, .8);
      cairo_set_dash(cr, dashed, 0, 0);
      cairo_stroke(cr);
    }
  }
}

gint _curve_sort_points(gconstpointer a, gconstpointer b)
{
  const int *am = (const int *)a;
  const int *bm = (const int *)b;
  if (am[1] == bm[1]) return am[0] - bm[0];
  return am[1] - bm[1];
}

int dt_curve_get_area(dt_iop_module_t *module, dt_dev_pixelpipe_iop_t *piece, dt_masks_form_t *form, int *width, int *height, int *posx, int *posy)
{
  return 0;
  /*float wd = piece->pipe->iwidth, ht = piece->pipe->iheight;
  
  //we get buffers for all points (by segments)
  float *points = malloc(60000*sizeof(float));
  const int nb = g_list_length(form->points);
  //int seg[nb];
  int pos = 0;
  //we render all segments
  for(int k = 0; k < nb; k++)
  {
    int k2 = (k+1)%nb;
    dt_masks_point_bezier_t *point1 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k);
    dt_masks_point_bezier_t *point2 = (dt_masks_point_bezier_t *)g_list_nth_data(form->points,k2);
    float p1[4] = {point1->corner[0]*wd, point1->corner[1]*ht, point1->ctrl2[0]*wd, point1->ctrl2[1]*ht};
    float p2[4] = {point2->corner[0]*wd, point2->corner[1]*ht, point2->ctrl1[0]*wd, point2->ctrl1[1]*ht};
    
    //we store the first point
    points[pos++] = p1[0];
    points[pos++] = p1[1];
    
    //and we determine all points by recursion (to be sure the distance between 2 points is <=1)
    float rx,ry;
    _curve_points_recurs(p1,p2,0.0,1.0,p1[0],p1[1],p2[0],p2[1],&rx,&ry,points,&pos);
  }
  
  if (!dt_dev_distort_transform_plus(module->dev,piece->pipe,0,module->priority,points,pos/2))
  {
    free(points);
    return 0;
  }
  
  //now we want to find the area, so we search min/max points
  float xmin, xmax, ymin, ymax;
  xmin = ymin = FLT_MAX;
  xmax = ymax = FLT_MIN;
  for (int i=0; i < pos/2; i++)
  {
    xmin = fminf(points[i*2],xmin);
    xmax = fmaxf(points[i*2],xmax);
    ymin = fminf(points[i*2+1],ymin);
    ymax = fmaxf(points[i*2+1],ymax);
  }
  *height = ymax-ymin+1;
  *width = xmax-xmin+1;
  *posx = xmin;
  *posy = ymin;*/
  return 1;
}

static void _curve_falloff(float **buffer, int *p0, int *p1, int posx, int posy, int bw)
{
  //segment length
  int l = sqrt((p1[0]-p0[0])*(p1[0]-p0[0])+(p1[1]-p0[1])*(p1[1]-p0[1]))+1;
  
  float lx = p1[0]-p0[0];
  float ly = p1[1]-p0[1];
  
  for (int i=0 ; i<l; i++)
  {
    //position
    int x = (int)((float)i*lx/(float)l) + p0[0] - posx;
    int y = (int)((float)i*ly/(float)l) + p0[1] - posy;
    float op = 1.0-(float)i/(float)l;
    //op = op*op*(3.0f - 2.0f*op);
    (*buffer)[y*bw+x] = op;
    if (x > 0) (*buffer)[y*bw+x-1] = op; //this one is to avoid gap due to int rounding
  }
}

int dt_curve_get_mask(dt_iop_module_t *module, dt_dev_pixelpipe_iop_t *piece, dt_masks_form_t *form, float **buffer, int *width, int *height, int *posx, int *posy)
{
  //we get buffers for all points
  float *points, *border;
  int points_count,border_count;
  if (!_curve_get_points_border(module->dev,form,module->priority,piece->pipe,&points,&points_count,&border,&border_count)) return 0;
  
  //now we want to find the area, so we search min/max points
  float xmin, xmax, ymin, ymax;
  xmin = ymin = FLT_MAX;
  xmax = ymax = FLT_MIN;
  int nb_corner = g_list_length(form->points);
  for (int i=nb_corner*3; i < border_count; i++)
  {
    xmin = fminf(border[i*2],xmin);
    xmax = fmaxf(border[i*2],xmax);
    ymin = fminf(border[i*2+1],ymin);
    ymax = fmaxf(border[i*2+1],ymax);
  }
  *height = ymax-ymin+1;
  *width = xmax-xmin+1;
  *posx = xmin;
  *posy = ymin;
  
  //we create a new buffer for all the points, sorted by row values
  GList* pts = {NULL};
  int nbp = border_count;
  int lastx, lasty,lasty2;
  if (nbp>2)
  {
    lastx = (int) points[(nbp-1)*2];
    lasty = (int) points[(nbp-1)*2+1];
    lasty2 = (int) points[(nbp-2)*2+1];
  
    for (int i=nb_corner*3; i < nbp; i++)
    {
      int xx = (int) points[i*2];
      int yy = (int) points[i*2+1];
      int *p = malloc(2*sizeof(int));
      p[0] = xx, p[1] = yy;
      
      if (lasty != INT_MIN)
      {
        //we don't store the point if it has the same y value as the last one
        if (yy == lasty) continue;
        
        //we want to be sure that there is no y jump
        if (yy-lasty > 1 || yy-lasty < -1)
        {
          if (yy<lasty)
          {
            for (int j=yy+1; j<lasty; j++)
            {
              int *pp = malloc(2*sizeof(int));
              pp[0] = (j-yy)*(lastx-xx)/(float)(lasty-yy)+xx, pp[1] = j;
              pts = g_list_append(pts,pp);
            }
          }
          else
          {
            for (int j=lasty+1; j<yy; j++)
            {
              int *pp = malloc(2*sizeof(int));
              pp[0] = (j-lasty)*(xx-lastx)/(float)(yy-lasty)+lastx, pp[1] = j;
              pts = g_list_append(pts,pp);
            }
          }
        }
        
        if (lasty2 != INT_MIN)
        {
          //if we change the direction of the curve (in y), then we add a extra point
          if ((lasty-lasty2)*(lasty-yy)>0)
          {
            int *pp = malloc(2*sizeof(int));
            pp[0] = lastx, pp[1] = lasty;
            pts = g_list_append(pts,pp);
          }
        }
      }
      //we add the point
      pts = g_list_append(pts,p);
      //printf("tabl %d %d\n",p[0],p[1]);
      //we change last values
      lasty2 = lasty;
      lasty = yy;
      lastx = xx;
    }
  }
  //and we sort all the datas
  pts = g_list_sort(pts,_curve_sort_points);
  
  //we allocate the buffer
  *buffer = malloc((*width)*(*height)*sizeof(float));
  
  //we populate the buffer row by row
  GList *ppts = g_list_first(pts);
  while(ppts)
  {
    //we get the first point
    int *p1,*p2;
    p1 = (int *)ppts->data;
    //we get the second point
    ppts = g_list_next(ppts);
    if (!ppts) break;
    p2 = (int *)ppts->data;
    
    //are the points on the same line ?
    if (p1[1] != p2[1]) 
    {
      continue;
    }
    
    //we set all the points between p1 and p2 to 1.0f
    for (int i=p1[0]; i<p2[0]; i++)
    {
      (*buffer)[(*width)*(p1[1]-(*posy)) + i-(*posx)] = 1.0f;
    }
    ppts = g_list_next(ppts);
  }
  
  //now we fill the falloff
  int pos = nb_corner*6;
  int p0[2], p1[2];
  int last0[2] = {-100,-100}, last1[2] = {-100,-100};
  for (int k=0; k<nb_corner; k++)
  {
    int pos1 = -border[k*6+2];
    int pos2 = -border[k*6+4];
    //from pos to pos1
    p1[0] = border[pos1], p1[1] = border[pos1+1];
    for (int i=pos; i<pos1; i+=2)
    {
      p0[0] = points[i], p0[1] = points[i+1];
      if (last0[0] != p0[0] || last0[1] != p0[1] || last1[0] != p1[0] || last1[1] != p1[1])
      {
        _curve_falloff(buffer,p0,p1,*posx,*posy,*width);
        last0[0] = p0[0], last0[1] = p0[1];
        last1[0] = p1[0], last1[1] = p1[1];
      }
    }
    pos = pos1;
    //from pos1 to pos2
    for (int i=pos1; i<pos2; i+=2)
    {
      p0[0] = points[i], p0[1] = points[i+1];
      p1[0] = border[i], p1[1] = border[i+1];
      if (last0[0] != p0[0] || last0[1] != p0[1] || last1[0] != p1[0] || last1[1] != p1[1])
      {
        _curve_falloff(buffer,p0,p1,*posx,*posy,*width);
        last0[0] = p0[0], last0[1] = p0[1];
        last1[0] = p1[0], last1[1] = p1[1];
      }
    }
    pos = pos2;
  }
  //now if pos != border_count
  p1[0] = border[pos], p1[1] = border[pos+1];
  for (int i=pos; i<border_count*2; i+=2)
  {
    p0[0] = points[i], p0[1] = points[i+1];
    if (last0[0] != p0[0] || last0[1] != p0[1] || last1[0] != p1[0] || last1[1] != p1[1])
    {
      _curve_falloff(buffer,p0,p1,*posx,*posy,*width);
      last0[0] = p0[0], last0[1] = p0[1];
      last1[0] = p1[0], last1[1] = p1[1];
    }
  }
  
  free(points);
  free(border);
  return 1;
}

void dt_curve_get_distance(float x, int y, float as, dt_masks_form_gui_t *gui, int corner_count, int *inside, int *inside_border, int *near)
{
  //we first check if it's inside borders
  int nb = 0;
  int last = -9999;
  *inside_border = 0;
  *near = -1;
  
  for (int i=corner_count*3; i<gui->border_count; i++)
  {
    int yy = (int) gui->border[i*2+1];
    if (yy != last && yy == y)
    {
      if (gui->border[i*2] > x) nb++;
    }
    last = yy;
  }
  *inside_border = (nb & 1); 
  
  //and we check if it's inside form
  int seg = 1;
  nb=0;
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
  if (*inside_border) *inside = 1;
}
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
