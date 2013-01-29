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

static void _lib_masks_click_callback(GtkWidget *widget, dt_masks_form_t *form)
{
  dt_masks_init_formgui(darktable.develop);
  darktable.develop->form_visible = form;
  dt_control_queue_redraw_center();
}

static void _lib_history_change_callback(gpointer instance, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_masks_t *d = (dt_lib_masks_t *)self->data;

  /* first destroy all buttons in list */
  gtk_container_foreach(GTK_CONTAINER(d->vbox),(GtkCallback)gtk_widget_destroy,0);

  /* iterate over history items and add them to list*/
  GList *forms = g_list_first(darktable.develop->forms);
  while(forms)
  {
    dt_masks_form_t *form = (dt_masks_form_t *)forms->data;
    GtkWidget *evb = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(evb), gtk_label_new(form->name));
    g_signal_connect(G_OBJECT(evb), "button-press-event", G_CALLBACK(_lib_masks_click_callback), form);
    gtk_box_pack_start (GTK_BOX (d->vbox),gtk_label_new(form->name),FALSE,FALSE,0);
    forms = g_list_next(forms);
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
