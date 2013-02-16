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

static void _lib_masks_recreate_list(dt_lib_module_t *self);

typedef struct dt_lib_masks_t
{
  /* vbox with managed history items */
  GtkWidget *vbox;
  GtkWidget *treeview;
  int gui_reset;
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

static void _tree_add_circle(GtkButton *button, dt_iop_module_t *module)
{
  //we create the new form
  dt_masks_form_t *spot = dt_masks_create(DT_MASKS_CIRCLE);
  dt_masks_change_form_gui(spot);
  darktable.develop->form_gui->creation = TRUE;
  darktable.develop->form_gui->creation_module = module;
  darktable.develop->form_gui->group_selected = 0;
  dt_control_queue_redraw_center();
}

static void _tree_add_curve(GtkButton *button, dt_iop_module_t *module)
{
  //we create the new form
  dt_masks_form_t *spot = dt_masks_create(DT_MASKS_CURVE);
  dt_masks_change_form_gui(spot);
  darktable.develop->form_gui->creation = TRUE;
  darktable.develop->form_gui->creation_module = module;
  darktable.develop->form_gui->group_selected = 0;
  dt_control_queue_redraw_center();
}

static void _tree_group(GtkButton *button, dt_lib_module_t *self)
{
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  //we create the new group
  dt_masks_form_t *grp = dt_masks_create(DT_MASKS_GROUP);
  snprintf(grp->name,128,"group #%d",g_list_length(darktable.develop->forms));
  
  //we add all selected forms to this group
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview));
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lm->treeview));

  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (model,&iter,item))
    {
      GValue gv = {0,};
      gtk_tree_model_get_value (model,&iter,2,&gv);
      int id = g_value_get_int(&gv);
      if (id > 0)
      {
        dt_masks_point_group_t *fpt = (dt_masks_point_group_t *) malloc(sizeof(dt_masks_point_group_t));
        fpt->formid = id;
        fpt->state = DT_MASKS_STATE_USE;
        grp->points = g_list_append(grp->points,fpt);
      }      
    }
    items = g_list_next(items);
  }
  
  //we add this group to the general list
  darktable.develop->forms = g_list_append(darktable.develop->forms,grp);
  
  //add we save
  dt_masks_write_forms(darktable.develop);
  _lib_masks_recreate_list(self);
  //dt_masks_change_form_gui(grp);
}

static void _tree_delete_shape(GtkButton *button, dt_lib_module_t *self)
{
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  
  //we first discard all visible shapes
  dt_masks_init_formgui(darktable.develop);
  darktable.develop->form_visible = NULL;
  
  //now we go throught all selected nodes
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview));
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lm->treeview));
  dt_iop_module_t *module = NULL;
  lm->gui_reset = 1;
  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (model,&iter,item))
    {
      GValue gv = {0,};
      gtk_tree_model_get_value (model,&iter,2,&gv);
      int id = g_value_get_int(&gv);
      GValue gv2 = {0,};
      gtk_tree_model_get_value (model,&iter,1,&gv2);
      if (G_VALUE_TYPE(&gv2) == G_TYPE_POINTER)
      {
        module = (dt_iop_module_t *)g_value_get_pointer(&gv2);
      }
      if (module)
      {
        int ok = 0;
        for (int i=0; i<module->blend_params->forms_count; i++)
        {
          if (module->blend_params->forms[i] == id) ok = 1;
          if (ok) module->blend_params->forms[i] = module->blend_params->forms[i+1];
        }
        if (ok)
        {
          module->blend_params->forms_count--;
          dt_masks_iop_update(module);
          dt_dev_add_history_item(darktable.develop, module, TRUE);
        }
      }
      else
      {
        //we drop the form from all modules
        GList *iops = g_list_first(darktable.develop->iop);
        while(iops)
        {
          dt_iop_module_t *m = (dt_iop_module_t *)iops->data;
          if (m->flags() & IOP_FLAGS_SUPPORTS_BLENDING)
          {
            int ok = 0;
            for (int i=0; i<m->blend_params->forms_count; i++)
            {
              if (m->blend_params->forms[i] == id) ok = 1;
              if (ok) m->blend_params->forms[i] = m->blend_params->forms[i+1];
            }
            if (ok)
            {
              m->blend_params->forms_count--;
              dt_masks_iop_update(m);
              dt_dev_add_history_item(darktable.develop, m, TRUE);
            }
          }
          iops = g_list_next(iops);
        }
        //we drop the form from the general list
        GList *forms = g_list_first(darktable.develop->forms);
        while (forms)
        {
          dt_masks_form_t *form = (dt_masks_form_t *)forms->data;
          if (form->formid == id)
          {
            darktable.develop->forms = g_list_remove(darktable.develop->forms,form);
            dt_masks_write_forms(darktable.develop);
            break;
          }
          forms = g_list_next(forms);
        }
      }
    }
    items = g_list_next(items);
  }
  lm->gui_reset = 0;
  _lib_masks_recreate_list(self);
}

static void _tree_selection_change (GtkTreeSelection *selection,dt_lib_masks_t *self)
{
  if (self->gui_reset) return;
  //if selection empty, we hide all
  int nb = gtk_tree_selection_count_selected_rows(selection);
  if (nb == 0)
  {
    dt_masks_init_formgui(darktable.develop);
    darktable.develop->form_visible = NULL;    
    dt_control_queue_redraw_center();
    return;
  }
  
  //else, we create a new from group with the selection and display it
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(self->treeview));
  dt_masks_form_t *grp = dt_masks_create(DT_MASKS_GROUP);
  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (model,&iter,item))
    {
      GValue gv = {0,};
      gtk_tree_model_get_value (model,&iter,2,&gv);
      int id = g_value_get_int(&gv);
      if (id > 0)
      {
        //we get the corresponding form
        dt_masks_form_t *form = dt_masks_get_from_id(darktable.develop,id);
        if (form)
        {
          if (form->type == DT_MASKS_GROUP)
          {
            GList *sforms = g_list_first(form->points);
            while(sforms)
            {
              dt_masks_point_group_t *fpt = (dt_masks_point_group_t *)sforms->data;
              dt_masks_point_group_t *fpt2 = (dt_masks_point_group_t *) malloc(sizeof(dt_masks_point_group_t));
              fpt2->formid = fpt->formid;
              fpt2->state = fpt->state;
              grp->points = g_list_append(grp->points,fpt2);
              sforms = g_list_next(sforms);
            }
          }
          else
          {
            dt_masks_point_group_t *fpt = (dt_masks_point_group_t *) malloc(sizeof(dt_masks_point_group_t));
            fpt->formid = id;
            fpt->state = DT_MASKS_STATE_USE;
            grp->points = g_list_append(grp->points,fpt);
          }
        }
      }
    }
    items = g_list_next(items); 
  }
  dt_masks_init_formgui(darktable.develop);
  darktable.develop->form_visible = grp;    
  dt_control_queue_redraw_center();   
}

static int _tree_button_pressed (GtkWidget *treeview, GdkEventButton *event, dt_lib_module_t *self)
{
  //dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  
  /* single click with the right mouse button? */
  if (event->type == GDK_BUTTON_PRESS  &&  event->button == 3)
  {
    //we first need to adjust selection
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));
    
    GtkTreePath *mouse_path = NULL;
    GtkTreeIter iter;
    dt_iop_module_t *module = NULL;
    int from_base = 0;
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview), (gint) event->x, (gint) event->y, &mouse_path, NULL, NULL, NULL))
    {
      //we retrive the iter and module from path
      if (gtk_tree_model_get_iter (model,&iter,mouse_path))
      {
        GValue gv = {0,};
        gtk_tree_model_get_value (model,&iter,1,&gv);
        module = g_value_peek_pointer(&gv);
      }
      //if this is a primary node, then no selection change
      if (gtk_tree_path_get_depth(mouse_path) > 1)
      {
        //if we are already inside the selection, no change
        if (!gtk_tree_selection_path_is_selected(selection,mouse_path))
        {
          if (!(event->state & GDK_CONTROL_MASK)) gtk_tree_selection_unselect_all(selection);
          gtk_tree_selection_select_path(selection, mouse_path);
          gtk_tree_path_free(mouse_path);
        }
      }
      else from_base = 1;
    }
    else return 0;
    
    //and we display the context-menu
    GtkWidget *menu, *item;
    menu = gtk_menu_new();
    
    //we get all infos from selection
    int nb = gtk_tree_selection_count_selected_rows(selection);
    //if (nb == 0) return 0;
    int from_all = 0;
    int from_group = 0;
    
    GtkTreePath *it0;
    if (from_base) it0 = mouse_path;
    else it0 = (GtkTreePath *)g_list_nth_data(gtk_tree_selection_get_selected_rows(selection,NULL),0);
    int *indices = gtk_tree_path_get_indices (it0);
    int depth = gtk_tree_path_get_depth (it0);
    if (depth > 2) from_group = 1;
    else if (depth == 1) from_base = 1;
    if (indices[0] == 0) from_all = 1;
    
    if (from_base)
    {
      item = gtk_menu_item_new_with_label(_("add circle shape"));
      g_signal_connect(item, "activate",(GCallback) _tree_add_circle, module);
      gtk_menu_append(menu, item);
      
      item = gtk_menu_item_new_with_label(_("add curve shape"));
      g_signal_connect(item, "activate",(GCallback) _tree_add_curve, module);
      gtk_menu_append(menu, item);
      
      if (!from_all)
      {
        item = gtk_menu_item_new_with_label(_("add existing shape"));
        gtk_menu_append(menu, item);
        //existing forms
        GtkWidget *menu0 = gtk_menu_new();
        GList *forms = g_list_first(darktable.develop->forms);
        while (forms)
        {
          dt_masks_form_t *form = (dt_masks_form_t *)forms->data;
          char str[10000] = "";
          strcat(str,form->name);
          int nbuse = 0;
          
          //we search were this form is used
          GList *modules = g_list_first(darktable.develop->iop);
          while (modules)
          {
            dt_iop_module_t *m = (dt_iop_module_t *)modules->data;
            
            if (m->blend_params)
            {
              for (int i=0; i<m->blend_params->forms_count; i++)
              {
                if (m->blend_params->forms[i] == form->formid)
                {
                  if (m == darktable.develop->gui_module)
                  {
                    nbuse = -1;
                    break;
                  }
                  if (nbuse==0) strcat(str," (");
                  strcat(str," ");
                  strcat(str,m->name());
                  nbuse++;
                }
              }
            }
            modules = g_list_next(modules);
          }
          if (nbuse != -1)
          {
            if (nbuse>0) strcat(str," )");
            
            //we add the menu entry
            item = gtk_menu_item_new_with_label(str);
            //g_object_set_data(G_OBJECT(item), "formid", GUINT_TO_POINTER(form->formid));
            //g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (_menu_add_exist), form);
            gtk_menu_append(menu0, item);
          }
          forms = g_list_next(forms);
        }
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu0);
      }
      
      gtk_menu_append(menu, gtk_separator_menu_item_new());
    }
    if (from_all)
    {
      item = gtk_menu_item_new_with_label(_("delete this shape"));
      g_signal_connect(item, "activate",(GCallback) _tree_delete_shape, self);
      gtk_menu_append(menu, item);
    }
    else
    {
      item = gtk_menu_item_new_with_label(_("remove from module"));
      g_signal_connect(item, "activate",(GCallback) _tree_delete_shape, self);
      gtk_menu_append(menu, item);
    }
    
    if (nb>1 && from_all)
    {
      gtk_menu_append(menu, gtk_separator_menu_item_new());
      item = gtk_menu_item_new_with_label(_("group the forms"));
      g_signal_connect(item, "activate",(GCallback) _tree_group, self);
      gtk_menu_append(menu, item);
    }
        
    
    if (from_group || !from_all)
    {
      gtk_menu_append(menu, gtk_separator_menu_item_new());
      item = gtk_menu_item_new_with_label(_("use inversed shape"));
      //g_signal_connect(item, "activate",(GCallback) view_popup_menu_onDoSomething, treeview);
      gtk_menu_append(menu, item);
      item = gtk_menu_item_new_with_label(_("move up"));
      //g_signal_connect(item, "activate",(GCallback) view_popup_menu_onDoSomething, treeview);
      gtk_menu_append(menu, item);
      item = gtk_menu_item_new_with_label(_("move down"));
      //g_signal_connect(item, "activate",(GCallback) view_popup_menu_onDoSomething, treeview);
      gtk_menu_append(menu, item);
    }
    gtk_widget_show_all(menu);
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,0, gdk_event_get_time((GdkEvent*)event));

    return 1;
  }

  return 0;
}

static gboolean _tree_restrict_select (GtkTreeSelection *selection, GtkTreeModel *model, GtkTreePath *path, gboolean path_currently_selected, gpointer data)
{
  dt_lib_masks_t *self = (dt_lib_masks_t *)data;
  if (self->gui_reset) return TRUE;
  
  //if the change is SELECT->UNSELECT no pb
  if (path_currently_selected) return TRUE;
  
  //we can't select a primary node
  int depth = gtk_tree_path_get_depth (path);
  if (depth == 1) return FALSE;
  
  //if selection is empty, no pb
  if (gtk_tree_selection_count_selected_rows(selection) == 0) return TRUE;
  
  //now we unselect all members of selection with not the same parent node
  //idem for all those with a different depth
  int *indices = gtk_tree_path_get_indices (path);
  
  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    int dd = gtk_tree_path_get_depth (item);
    int *ii = gtk_tree_path_get_indices (item);
    int ok = 1;
    if (dd != depth) ok = 0;
    else if (ii[dd-2] != indices[dd-2]) ok = 0;
    if (!ok)
    {
      gtk_tree_selection_unselect_path(selection,item);
      items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
      continue;
    }
    items = g_list_next(items);
  }
  return TRUE;
}

static void _lib_masks_recreate_list(dt_lib_module_t *self)
{
  //const int bs = 12;
  //dt_lib_masks_t *d = (dt_lib_masks_t *)self->data;

  /* first destroy all buttons in list */
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  if (lm->gui_reset) return;
  
  gtk_container_foreach(GTK_CONTAINER(self->widget),(GtkCallback)gtk_widget_destroy,0);
  
  //dt_iop_module_t *iop = darktable.develop->gui_module;
  
  GtkTreeStore *treestore;
  GtkTreeIter toplevel, child, child2;
  //we store : text ; *module ; formid
  treestore = gtk_tree_store_new(3, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_INT);
  
  //first, we display the "all shapes" entry
  gtk_tree_store_append(treestore, &toplevel, NULL);
  gtk_tree_store_set(treestore, &toplevel,0, _("all created shapes"),1,NULL,2,0,-1);

  GList *forms = g_list_first(darktable.develop->forms);
  while (forms)
  {
    dt_masks_form_t *form = (dt_masks_form_t *)forms->data;
    gtk_tree_store_append(treestore, &child, &toplevel);
    gtk_tree_store_set(treestore, &child, 0, form->name,1,NULL,2,form->formid, -1);
    if (form->type == DT_MASKS_GROUP)
    {
      GList *sforms = g_list_first(form->points);
      while(sforms)
      {
        dt_masks_point_group_t *fpt = (dt_masks_point_group_t *)sforms->data;
        dt_masks_form_t *sform = dt_masks_get_from_id(darktable.develop,fpt->formid);
        if (sform)
        {
          gtk_tree_store_append(treestore, &child2, &child);
          gtk_tree_store_set(treestore, &child2, 0, sform->name,1,NULL,2,sform->formid, -1);
        }
        sforms = g_list_next(sforms);
      }
    }
    forms = g_list_next(forms);
  }
  
  //now we display shapes iop by iop
  GList *iops = g_list_first(darktable.develop->iop);
  int act = -1;
  int pos = 1;
  while(iops)
  {
    dt_iop_module_t *module = (dt_iop_module_t *)iops->data;
    if ((module->flags() & IOP_FLAGS_SUPPORTS_BLENDING) && module->blend_params->forms_count > 0)
    {
      //we create the entry
      gtk_tree_store_append(treestore, &toplevel, NULL);
      gtk_tree_store_set(treestore, &toplevel,0, module->name(),1,module,2,0,-1);
      //ad we populate it
      for (int i=0; i<module->blend_params->forms_count; i++)
      {
        dt_masks_form_t *form = dt_masks_get_from_id(module->dev,module->blend_params->forms[i]);
        if (!form) continue;
        gtk_tree_store_append(treestore, &child, &toplevel);
        gtk_tree_store_set(treestore, &child, 0, form->name,1,module,2,form->formid, -1);
        if (form->type == DT_MASKS_GROUP)
        {
          GList *sforms = g_list_first(form->points);
          while(sforms)
          {
            dt_masks_point_group_t *fpt = (dt_masks_point_group_t *)sforms->data;
            dt_masks_form_t *sform = dt_masks_get_from_id(darktable.develop,fpt->formid);
            if (sform)
            {
              gtk_tree_store_append(treestore, &child2, &child);
              gtk_tree_store_set(treestore, &child2, 0, sform->name,1,module,2,sform->formid, -1);
            }
            sforms = g_list_next(sforms);
          }
        }
      }
      if (module == darktable.develop->gui_module) act = pos;
    }
    iops = g_list_next(iops);
    pos++;
  }
  
  lm->treeview = gtk_tree_view_new();
  GtkTreeViewColumn *col = gtk_tree_view_column_new();
  gtk_tree_view_column_set_title(col, "shapes");
  gtk_tree_view_append_column(GTK_TREE_VIEW(lm->treeview), col);
  
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_column_pack_start(col, renderer, TRUE);
  gtk_tree_view_column_add_attribute(col, renderer, "text", 0);

  gtk_tree_view_set_model(GTK_TREE_VIEW(lm->treeview), GTK_TREE_MODEL(treestore));
  g_object_unref(treestore);
  
  //we expand the "all" entry and the actual module one
  gtk_tree_view_expand_row (GTK_TREE_VIEW(lm->treeview),gtk_tree_path_new_from_indices (0,-1),FALSE);
  if (act >= 0) gtk_tree_view_expand_row (GTK_TREE_VIEW(lm->treeview),gtk_tree_path_new_from_indices (act,-1),FALSE);
  
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lm->treeview));
  gtk_tree_selection_set_mode(selection,GTK_SELECTION_MULTIPLE);
  gtk_tree_selection_set_select_function(selection,_tree_restrict_select,lm,NULL);
  GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(lm->treeview), FALSE);
  gtk_widget_set_size_request(lm->treeview, -1, 300);
  gtk_container_add(GTK_CONTAINER(sw), lm->treeview);
  
  gtk_box_pack_start(GTK_BOX(self->widget), sw, TRUE, TRUE, 1);

  g_signal_connect(selection, "changed", G_CALLBACK(_tree_selection_change), lm);
  g_signal_connect(lm->treeview, "button-press-event", (GCallback) _tree_button_pressed, self);
    
  /* show all widgets */
  gtk_widget_show_all(sw);
}
static void _lib_history_change_callback(gpointer instance, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  _lib_masks_recreate_list(self);
}

static int _lib_masks_selection_change_foreach (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
  dt_lib_masks_t *self = (dt_lib_masks_t *)data;
  //we only select nodes from the "all shapes" node
  int *indices = gtk_tree_path_get_indices (path);
  int depth = gtk_tree_path_get_depth (path);
  if (depth < 2) return 0;
  if (indices[0] != 0) return 0;
  
  //we get the formid
  GValue gv = {0,};
  gtk_tree_model_get_value (model,iter,2,&gv);
  int id = g_value_get_int(&gv);
  if (id <= 0) return 0;
  
  dt_masks_form_t *form = darktable.develop->form_visible;
  if (!form) return 1;

  //if the id are equal, we select it and return
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(self->treeview));
  if (form->formid == id)
  {
    gtk_tree_selection_select_iter (selection,iter);
    return 1;
  }
  //if form->formid == 0 and form is a group, we search inside the group
  if (form->formid == 0 && form->type == DT_MASKS_GROUP)
  {
    GList *forms = g_list_first(form->points);
    while(forms)
    {
      dt_masks_point_group_t *fpt = (dt_masks_point_group_t *)forms->data;
      if (fpt->formid == id)
      {
        gtk_tree_selection_select_iter (selection,iter);
        return 0;
      }
      forms = g_list_next(forms);
    }
  }
  return 0;
}
static void _lib_masks_selection_change(dt_lib_module_t *self)
{
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  if (!lm->treeview) return;

  //we first unselect all
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lm->treeview));
  lm->gui_reset = 1;
  gtk_tree_selection_unselect_all(selection);
  lm->gui_reset = 0;

  //now we get the visible form
  dt_masks_form_t *form = darktable.develop->form_visible;
  if (!form) return;

  //we go throught all nodes
  lm->gui_reset = 1;
  gtk_tree_model_foreach (gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview)),_lib_masks_selection_change_foreach,lm);
  lm->gui_reset = 0;
}

void gui_init(dt_lib_module_t *self)
{
  /* initialize ui widgets */
  dt_lib_masks_t *d = (dt_lib_masks_t *)g_malloc(sizeof(dt_lib_masks_t));
  self->data = (void *)d;
  memset(d,0,sizeof(dt_lib_masks_t));
  d->gui_reset = 0;
  
  //dt_iop_module_t *iop = darktable.develop->gui_module;

  self->widget =  gtk_vbox_new (FALSE,2);
  
  //d->vbox = gtk_vbox_new(FALSE,0);
  //gtk_box_pack_start (GTK_BOX (self->widget),d->vbox,FALSE,FALSE,0);

  gtk_widget_show_all (self->widget);

  /* connect to history change signal for updating the history view */
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_HISTORY_CHANGE, G_CALLBACK(_lib_history_change_callback), self);

  // set proxy functions
  darktable.develop->proxy.masks.module = self;
  darktable.develop->proxy.masks.switch_module = _lib_masks_recreate_list;
  darktable.develop->proxy.masks.selection_change = _lib_masks_selection_change;
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
