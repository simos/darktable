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

#include "common/darktable.h"
#include "common/debug.h"
#include "control/control.h"
#include "control/conf.h"
#include "common/styles.h"
#include "develop/develop.h"
#include "develop/masks.h"
#include "develop/imageop.h"
#include "libs/lib.h"
#include "gui/gtk.h"
#include "gui/styles.h"
#include "dtgtk/button.h"

DT_MODULE(1)


typedef struct dt_lib_masks_t
{
  /* vbox with managed history items */
  GtkWidget *title;
  GtkWidget *vbox;
}
dt_lib_masks_t;


const char* name()
{
  return _("masks manager");
}

uint32_t views()
{
  return DT_VIEW_DARKROOM;
}

uint32_t container()
{
  return DT_UI_CONTAINER_PANEL_LEFT_CENTER;
}

int position()
{
  return 10;
}

static void _lib_masks_click_callback(GtkWidget *widget, GdkEventButton *e, dt_masks_form_t *form)
{
  int select = 1;
  dt_masks_init_formgui(darktable.develop);
  if (darktable.develop->form_visible == form)
  {
    select = 0;
    darktable.develop->form_visible = NULL;
  }
  else darktable.develop->form_visible = form;
  
  //set colors
  GtkWidget *hb = gtk_widget_get_parent(widget);
  GtkWidget *vb = gtk_widget_get_parent(hb);

  GList *childs = gtk_container_get_children(GTK_CONTAINER(vb));
  while(childs)
  {
    GtkWidget *w = (GtkWidget *) childs->data;
    GtkWidget *evb = (GtkWidget *) g_list_nth_data(gtk_container_get_children(GTK_CONTAINER(w)),1);
    GtkWidget *lb = (GtkWidget *) g_list_nth_data(gtk_container_get_children(GTK_CONTAINER(evb)),0);
    if (w == hb && select)
    {
      char *markup = g_markup_printf_escaped ("<b>%s</b>", gtk_label_get_text(GTK_LABEL(lb)));
      gtk_label_set_markup (GTK_LABEL (lb), markup);
      g_free (markup);
      //GtkStyle *style = gtk_widget_get_style(lb);
      //gtk_widget_modify_bg(lb, GTK_STATE_SELECTED, &style->bg[GTK_STATE_NORMAL]);
    }
    else
    {
      char *markup = g_markup_printf_escaped ("%s", gtk_label_get_text(GTK_LABEL(lb)));
      gtk_label_set_markup (GTK_LABEL (lb), markup);
      g_free (markup);
    }    
    childs = g_list_next(childs);
  }   
    
  dt_control_queue_redraw_center();
}
static void _lib_masks_show_toggle_callback(GtkWidget *widget, dt_masks_form_t *form)
{
  //we search the form pos in current module
  dt_iop_module_t *iop = darktable.develop->gui_module;
  if (!iop) return;
  
  int pos = -1;
  for (int i=0; i<iop->blend_params->forms_count; i++)
  {
    if (iop->blend_params->forms[i] == form->formid)
    {
      pos = i;
      break;
    }
  }
  if (pos <0) return;
  
  if (iop->blend_params->forms_state[pos] & DT_MASKS_STATE_USE)
  {
    iop->blend_params->forms_state[pos] -= DT_MASKS_STATE_USE;
  }
  else
  {
    iop->blend_params->forms_state[pos] += DT_MASKS_STATE_USE;
  }
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), (iop->blend_params->forms_state[pos] & DT_MASKS_STATE_USE));
  dt_dev_add_history_item(darktable.develop, iop, TRUE);
}

static void _lib_history_change_callback(gpointer instance, gpointer user_data)
{
  const int bs = 12;
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_masks_t *d = (dt_lib_masks_t *)self->data;

  /* first destroy all buttons in list */
  gtk_container_foreach(GTK_CONTAINER(d->vbox),(GtkCallback)gtk_widget_destroy,0);
  
  dt_iop_module_t *iop = darktable.develop->gui_module;
  if (!iop) return;
  
  //we update the title
  gtk_label_set_text(GTK_LABEL(d->title),iop->name());
  
  /* iterate over history items and add them to list*/
  for (int i=0; i<iop->blend_params->forms_count; i++)
  {
    dt_masks_form_t *form = dt_masks_get_from_id(darktable.develop,iop->blend_params->forms[i]);
    if (!form) continue;
    GtkWidget *hb = gtk_hbox_new(FALSE,0);
    
    GtkWidget *item = dtgtk_togglebutton_new(dtgtk_cairo_paint_eye_masks, CPF_STYLE_FLAT|CPF_DO_NOT_USE_BORDER);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(item), (iop->blend_params->forms_state[i] & DT_MASKS_STATE_USE));
    g_signal_connect (G_OBJECT (item), "toggled", G_CALLBACK (_lib_masks_show_toggle_callback), form);
    gtk_widget_set_size_request(item,bs,bs);
    
    GtkWidget *evb = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(evb), gtk_label_new(form->name));
    g_signal_connect (G_OBJECT (evb), "button-press-event", G_CALLBACK(_lib_masks_click_callback), form);
    
    gtk_box_pack_start (GTK_BOX (hb),item,FALSE,FALSE,2);
    gtk_box_pack_start (GTK_BOX (hb),evb,FALSE,FALSE,2);
    
    gtk_box_pack_start (GTK_BOX (d->vbox),hb,FALSE,FALSE,0);
  }

  /* show all widgets */
  gtk_widget_show_all(d->vbox);
}

void gui_init(dt_lib_module_t *self)
{
  /* initialize ui widgets */
  dt_lib_masks_t *d = (dt_lib_masks_t *)g_malloc(sizeof(dt_lib_masks_t));
  self->data = (void *)d;
  memset(d,0,sizeof(dt_lib_masks_t));

  dt_iop_module_t *iop = darktable.develop->gui_module;

  self->widget =  gtk_vbox_new (FALSE,2);
  if (iop) d->title = gtk_label_new(iop->name());
  else d->title = gtk_label_new(_("no module selected"));
  d->vbox = gtk_vbox_new(FALSE,0);
  
  //populate the vbox
  GList *forms = g_list_first(darktable.develop->forms);
  while(forms)
  {
    dt_masks_form_t *form = (dt_masks_form_t *)forms->data;
    GtkWidget *evb = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(evb), gtk_label_new(form->name));
    g_signal_connect(G_OBJECT(evb), "button-press-event", G_CALLBACK(_lib_masks_click_callback), form);
    gtk_box_pack_start (GTK_BOX (d->vbox),evb,FALSE,FALSE,0);
    forms = g_list_next(forms);
  }

  /* add history list and buttonbox to widget */
  gtk_box_pack_start (GTK_BOX (self->widget),d->title,FALSE,FALSE,0);
  gtk_box_pack_start (GTK_BOX (self->widget),d->vbox,FALSE,FALSE,0);

  gtk_widget_show_all (self->widget);

  /* connect to history change signal for updating the history view */
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_HISTORY_CHANGE, G_CALLBACK(_lib_history_change_callback), self);

}

void gui_cleanup(dt_lib_module_t *self)
{
  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(_lib_history_change_callback), self);

  g_free(self->data);
  self->data = NULL;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
