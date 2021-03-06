/* GTK - The GIMP Toolkit
 * Copyright © 2014 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "gtkpopovermenu.h"
#include "gtkpopovermenuprivate.h"

#include "gtkstack.h"
#include "gtkstylecontext.h"
#include "gtkintl.h"
#include "gtkmenusectionboxprivate.h"
#include "gtkmenubutton.h"
#include "gtkactionmuxerprivate.h"
#include "gtkmenutrackerprivate.h"
#include "gtkpopoverprivate.h"
#include "gtkwidgetprivate.h"
#include "gtkeventcontrollerfocus.h"
#include "gtkeventcontrollermotion.h"
#include "gtkmain.h"
#include "gtktypebuiltins.h"
#include "gtkbindings.h"
#include "gtkmodelbuttonprivate.h"
#include "gtkpopovermenubar.h"


/**
 * SECTION:gtkpopovermenu
 * @Short_description: Popovers to use as menus
 * @Title: GtkPopoverMenu
 *
 * GtkPopoverMenu is a subclass of #GtkPopover that treats its
 * children like menus and allows switching between them. It
 * can open submenus as traditional, nested submenus, or in a
 * more touch-friendly sliding fashion.
 *
 * GtkPopoverMenu is meant to be used primarily with menu models,
 * using gtk_popover_menu_new_from_model(). If you need to put other
 * widgets such as #GtkSpinButton or #GtkSwitch into a popover,
 * use a #GtkPopover.
 *
 * In addition to all the regular menu model features, this function
 * supports rendering sections in the model in a more compact form,
 * as a row of image buttons instead of menu items.
 *
 * To use this rendering, set the ”display-hint” attribute of the
 * section to ”horizontal-buttons” and set the icons of your items
 * with the ”verb-icon” attribute.
 *
 * # CSS Nodes
 *
 * #GtkPopoverMenu is just a subclass of #GtkPopover that adds
 * custom content to it, therefore it has the same CSS nodes.
 * It is one of the cases that add a .menu style class to
 * the popover's main node.
 */

typedef struct _GtkPopoverMenuClass GtkPopoverMenuClass;

struct _GtkPopoverMenu
{
  GtkPopover parent_instance;

  GtkWidget *active_item;
  GtkWidget *open_submenu;
  GtkWidget *parent_menu;
  GMenuModel *model;
  GtkPopoverMenuFlags flags;
};

struct _GtkPopoverMenuClass
{
  GtkPopoverClass parent_class;
};

enum {
  PROP_VISIBLE_SUBMENU = 1,
  PROP_MENU_MODEL
};

G_DEFINE_TYPE (GtkPopoverMenu, gtk_popover_menu, GTK_TYPE_POPOVER)

GtkWidget *
gtk_popover_menu_get_parent_menu (GtkPopoverMenu *menu)
{
  return menu->parent_menu;
}

void
gtk_popover_menu_set_parent_menu (GtkPopoverMenu *menu,
                                  GtkWidget      *parent)
{
  menu->parent_menu = parent;
}

GtkWidget *
gtk_popover_menu_get_open_submenu (GtkPopoverMenu *menu)
{
  return menu->open_submenu;
}

void
gtk_popover_menu_set_open_submenu (GtkPopoverMenu *menu,
                                   GtkWidget      *submenu)
{
  menu->open_submenu = submenu;
}

GtkWidget *
gtk_popover_menu_get_active_item (GtkPopoverMenu *menu)
{
  return menu->active_item;
}

void
gtk_popover_menu_set_active_item (GtkPopoverMenu *menu,
                                  GtkWidget      *item)
{
  if (menu->active_item != item)
    {
      if (menu->active_item)
        {
          gtk_widget_unset_state_flags (menu->active_item, GTK_STATE_FLAG_SELECTED);
          g_object_remove_weak_pointer (G_OBJECT (menu->active_item), (gpointer *)&menu->active_item);
        }

      menu->active_item = item;

      if (menu->active_item)
        {
          GtkWidget *popover;

          g_object_add_weak_pointer (G_OBJECT (menu->active_item), (gpointer *)&menu->active_item);

          gtk_widget_set_state_flags (menu->active_item, GTK_STATE_FLAG_SELECTED, FALSE);
          if (GTK_IS_MODEL_BUTTON (item))
            g_object_get (item, "popover", &popover, NULL);
          else
            popover = NULL;

          if (!popover || popover != menu->open_submenu)
            gtk_widget_grab_focus (menu->active_item);

          g_clear_object (&popover);
       }
    }
}

static void
visible_submenu_changed (GObject        *object,
                         GParamSpec     *pspec,
                         GtkPopoverMenu *popover)
{
  g_object_notify (G_OBJECT (popover), "visible-submenu");
}

static void
focus_out (GtkEventController   *controller,
           GtkPopoverMenu       *menu)
{
  GtkWidget *new_focus = gtk_root_get_focus (gtk_widget_get_root (GTK_WIDGET (menu)));

  if (!gtk_event_controller_focus_contains_focus (GTK_EVENT_CONTROLLER_FOCUS (controller)) &&
      new_focus != NULL)
    {
      if (menu->parent_menu &&
          GTK_POPOVER_MENU (menu->parent_menu)->open_submenu == (GtkWidget*) menu)
        GTK_POPOVER_MENU (menu->parent_menu)->open_submenu = NULL;
      gtk_popover_popdown (GTK_POPOVER (menu));
    }
}

static void
leave_cb (GtkEventController   *controller,
          GdkCrossingMode       mode,
          gpointer              data)
{
  GtkWidget *target;

  target = gtk_event_controller_get_widget (controller);

  gtk_popover_menu_set_active_item (GTK_POPOVER_MENU (target), NULL);
}

static void
gtk_popover_menu_init (GtkPopoverMenu *popover)
{
  GtkWidget *stack;
  GtkEventController *controller;

  stack = gtk_stack_new ();
  gtk_stack_set_vhomogeneous (GTK_STACK (stack), FALSE);
  gtk_stack_set_transition_type (GTK_STACK (stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
  gtk_stack_set_interpolate_size (GTK_STACK (stack), TRUE);
  gtk_container_add (GTK_CONTAINER (popover), stack);
  g_signal_connect (stack, "notify::visible-child-name",
                    G_CALLBACK (visible_submenu_changed), popover);

  gtk_widget_add_css_class (GTK_WIDGET (popover), "menu");

  controller = gtk_event_controller_focus_new ();
  g_signal_connect (controller, "leave", G_CALLBACK (focus_out), popover);
  gtk_widget_add_controller (GTK_WIDGET (popover), controller);

  controller = gtk_event_controller_motion_new ();
  g_signal_connect (controller, "leave", G_CALLBACK (leave_cb), popover);
  gtk_widget_add_controller (GTK_WIDGET (popover), controller);
}

static void
gtk_popover_menu_dispose (GObject *object)
{
  GtkPopoverMenu *popover = GTK_POPOVER_MENU (object);

  if (popover->active_item)
    {
      g_object_remove_weak_pointer (G_OBJECT (popover->active_item), (gpointer *)&popover->active_item);
      popover->active_item = NULL;
    }

  g_clear_object (&popover->model);

  G_OBJECT_CLASS (gtk_popover_menu_parent_class)->dispose (object);
}

static void
gtk_popover_menu_map (GtkWidget *widget)
{
  gtk_popover_menu_open_submenu (GTK_POPOVER_MENU (widget), "main");
  GTK_WIDGET_CLASS (gtk_popover_menu_parent_class)->map (widget);
}

static void
gtk_popover_menu_unmap (GtkWidget *widget)
{
  GTK_WIDGET_CLASS (gtk_popover_menu_parent_class)->unmap (widget);
  gtk_popover_menu_open_submenu (GTK_POPOVER_MENU (widget), "main");
}

static void
gtk_popover_menu_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  GtkWidget *stack;

  stack = gtk_bin_get_child (GTK_BIN (object));

  switch (property_id)
    {
    case PROP_VISIBLE_SUBMENU:
      g_value_set_string (value, gtk_stack_get_visible_child_name (GTK_STACK (stack)));
      break;

    case PROP_MENU_MODEL:
      g_value_set_object (value, gtk_popover_menu_get_menu_model (GTK_POPOVER_MENU (object)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gtk_popover_menu_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  GtkWidget *stack;

  stack = gtk_bin_get_child (GTK_BIN (object));

  switch (property_id)
    {
    case PROP_VISIBLE_SUBMENU:
      gtk_stack_set_visible_child_name (GTK_STACK (stack), g_value_get_string (value));
      break;

    case PROP_MENU_MODEL:
      gtk_popover_menu_set_menu_model (GTK_POPOVER_MENU (object), g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static gboolean
gtk_popover_menu_focus (GtkWidget        *widget,
                        GtkDirectionType  direction)
{
  if (gtk_widget_get_first_child (widget) == NULL)
    {
      return FALSE;
    }
  else
    {
      if (GTK_POPOVER_MENU (widget)->open_submenu)
        {
  g_print ("open submenu\n");
          if (gtk_widget_child_focus (GTK_POPOVER_MENU (widget)->open_submenu, direction))
            return TRUE;
          if (direction == GTK_DIR_LEFT)
            {
              gtk_widget_grab_focus (GTK_POPOVER_MENU (widget)->active_item);
              return TRUE;
            }
          return FALSE;
        }

      if (gtk_widget_focus_move (widget, direction))
        return TRUE;

      if (direction == GTK_DIR_LEFT || direction == GTK_DIR_RIGHT)
        {
          /* If we are part of a menubar, we want to let the
           * menubar use left/right arrows for cycling, else
           * we eat them.
           */
          if (gtk_widget_get_ancestor (widget, GTK_TYPE_POPOVER_MENU_BAR) ||
              (gtk_popover_menu_get_parent_menu (GTK_POPOVER_MENU (widget)) &&
               direction == GTK_DIR_LEFT))
            return FALSE;
          else
            return TRUE;
        }
      else if (direction == GTK_DIR_UP || direction == GTK_DIR_DOWN)
        {
          GtkWidget *p;

          /* cycle around */
          for (p = gtk_window_get_focus (GTK_WINDOW (gtk_widget_get_root (widget)));
               p != widget;
               p = gtk_widget_get_parent (p))
            {
              gtk_widget_set_focus_child (p, NULL);
            }
          if (gtk_widget_focus_move (widget, direction))
            return TRUE;
       }
    }

  return FALSE;
}


static void
add_tab_bindings (GtkBindingSet    *binding_set,
                  GdkModifierType   modifiers,
                  GtkDirectionType  direction)
{
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_Tab, modifiers,
                                "move-focus", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Tab, modifiers,
                                "move-focus", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
}

static void
add_arrow_bindings (GtkBindingSet    *binding_set,
                    guint             keysym,
                    GtkDirectionType  direction)
{
  guint keypad_keysym = keysym - GDK_KEY_Left + GDK_KEY_KP_Left;
 
  gtk_binding_entry_add_signal (binding_set, keysym, 0,
                                "move-focus", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
  gtk_binding_entry_add_signal (binding_set, keysym, GDK_CONTROL_MASK,
                                "move-focus", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
  gtk_binding_entry_add_signal (binding_set, keypad_keysym, 0,
                                "move-focus", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
  gtk_binding_entry_add_signal (binding_set, keypad_keysym, GDK_CONTROL_MASK,
                                "move-focus", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
}

static void
gtk_popover_menu_show (GtkWidget *widget)
{
  gtk_popover_menu_set_open_submenu (GTK_POPOVER_MENU (widget), NULL);

  GTK_WIDGET_CLASS (gtk_popover_menu_parent_class)->show (widget);
}

static void
gtk_popover_menu_class_init (GtkPopoverMenuClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkBindingSet *binding_set;

  object_class->dispose = gtk_popover_menu_dispose;
  object_class->set_property = gtk_popover_menu_set_property;
  object_class->get_property = gtk_popover_menu_get_property;

  widget_class->map = gtk_popover_menu_map;
  widget_class->unmap = gtk_popover_menu_unmap;
  widget_class->focus = gtk_popover_menu_focus;
  widget_class->show = gtk_popover_menu_show;

  g_object_class_install_property (object_class,
                                   PROP_VISIBLE_SUBMENU,
                                   g_param_spec_string ("visible-submenu",
                                                        P_("Visible submenu"),
                                                        P_("The name of the visible submenu"),
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
                                   PROP_MENU_MODEL,
                                   g_param_spec_object ("menu-model",
                                                        P_("Menu model"),
                                                        P_("The model from which the menu is made."),
                                                        G_TYPE_MENU_MODEL,
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  binding_set = gtk_binding_set_by_class (klass);

  add_arrow_bindings (binding_set, GDK_KEY_Up, GTK_DIR_UP);
  add_arrow_bindings (binding_set, GDK_KEY_Down, GTK_DIR_DOWN);
  add_arrow_bindings (binding_set, GDK_KEY_Left, GTK_DIR_LEFT);
  add_arrow_bindings (binding_set, GDK_KEY_Right, GTK_DIR_RIGHT);

  add_tab_bindings (binding_set, 0, GTK_DIR_TAB_FORWARD);
  add_tab_bindings (binding_set, GDK_CONTROL_MASK, GTK_DIR_TAB_FORWARD);
  add_tab_bindings (binding_set, GDK_SHIFT_MASK, GTK_DIR_TAB_BACKWARD);
  add_tab_bindings (binding_set, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_DIR_TAB_BACKWARD);

  gtk_binding_entry_add_signal (binding_set, GDK_KEY_Return, 0,
                                "activate-default", 0);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_ISO_Enter, 0,
                                "activate-default", 0);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Enter, 0,
                                "activate-default", 0);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_space, 0,
                                "activate-default", 0);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Space, 0,
                                "activate-default", 0);
}

/**
 * gtk_popover_menu_new:
 *
 * Creates a new popover menu.
 *
 * Returns: a new #GtkPopoverMenu
 */
GtkWidget *
gtk_popover_menu_new (void)
{
  GtkWidget *popover;

  popover = g_object_new (GTK_TYPE_POPOVER_MENU,
                          "autohide", TRUE,
                          NULL);

  return popover;
}

/*<private>
 * gtk_popover_menu_open_submenu:
 * @popover: a #GtkPopoverMenu
 * @name: the name of the menu to switch to
 *
 * Opens a submenu of the @popover. The @name
 * must be one of the names given to the submenus
 * of @popover with #GtkPopoverMenu:submenu, or
 * "main" to switch back to the main menu.
 *
 * #GtkModelButton will open submenus automatically
 * when the #GtkModelButton:menu-name property is set,
 * so this function is only needed when you are using
 * other kinds of widgets to initiate menu changes.
 */
void
gtk_popover_menu_open_submenu (GtkPopoverMenu *popover,
                               const gchar    *name)
{
  GtkWidget *stack;

  g_return_if_fail (GTK_IS_POPOVER_MENU (popover));

  stack = gtk_bin_get_child (GTK_BIN (popover));
  gtk_stack_set_visible_child_name (GTK_STACK (stack), name);
}

void
gtk_popover_menu_add_submenu (GtkPopoverMenu *popover,
                              GtkWidget      *submenu,
                              const char     *name)
{
  GtkWidget *stack;

  stack = gtk_bin_get_child (GTK_BIN (popover));
  gtk_stack_add_named (GTK_STACK (stack), submenu, name);
}

/**
 * gtk_popover_menu_new_from_model:
 * @model: (allow-none): a #GMenuModel, or %NULL
 *
 * Creates a #GtkPopoverMenu and populates it according to
 * @model.
 *
 * The created buttons are connected to actions found in the
 * #GtkApplicationWindow to which the popover belongs - typically
 * by means of being attached to a widget that is contained within
 * the #GtkApplicationWindows widget hierarchy.
 *
 * Actions can also be added using gtk_widget_insert_action_group()
 * on the menus attach widget or on any of its parent widgets.
 *
 * This function creates menus with sliding submenus.
 * See gtk_popover_menu_new_from_model_full() for a way
 * to control this.
 *
 * Returns: the new #GtkPopoverMenu
 */
GtkWidget *
gtk_popover_menu_new_from_model (GMenuModel *model)

{
  return gtk_popover_menu_new_from_model_full (model, 0);
}

/**
 * gtk_popover_menu_new_from_model_full:
 * @model: a #GMenuModel
 * @flags: flags that affect how the menu is created
 *
 * Creates a #GtkPopoverMenu and populates it according to
 * @model.
 *
 * The created buttons are connected to actions found in the
 * action groups that are accessible from the parent widget.
 * This includes the #GtkApplicationWindow to which the popover
 * belongs. Actions can also be added using gtk_widget_insert_action_group()
 * on the parent widget or on any of its parent widgets.
 *
 * The only flag that is supported currently is
 * #GTK_POPOVER_MENU_NESTED, which makes GTK create traditional,
 * nested submenus instead of the default sliding submenus.
 *
 * Returns: (transfer full): the new #GtkPopoverMenu
 */
GtkWidget *
gtk_popover_menu_new_from_model_full (GMenuModel          *model,
                                      GtkPopoverMenuFlags  flags)
{
  GtkWidget *popover;

  g_return_val_if_fail (model == NULL || G_IS_MENU_MODEL (model), NULL);

  popover = gtk_popover_menu_new ();
  GTK_POPOVER_MENU (popover)->flags = flags;
  gtk_popover_menu_set_menu_model (GTK_POPOVER_MENU (popover), model);

  return popover;
}

/**
 * gtk_popover_menu_set_model:
 * @popover: a #GtkPopoverMenu
 * @model: (nullable): a #GtkMenuModel, or %NULL
 *
 * Sets a new menu model on @popover.
 *
 * The existing contents of @popover are removed, and
 * the @popover is populated with new contents according
 * to @model.
 */
void
gtk_popover_menu_set_menu_model (GtkPopoverMenu *popover,
                                 GMenuModel     *model)
{
  g_return_if_fail (GTK_IS_POPOVER_MENU (popover));
  g_return_if_fail (model == NULL || G_IS_MENU_MODEL (model));

  if (g_set_object (&popover->model, model))
    {
      GtkWidget *stack;
      GtkWidget *child;

      stack = gtk_bin_get_child (GTK_BIN (popover));
      while ((child = gtk_widget_get_first_child (stack)))
        gtk_container_remove (GTK_CONTAINER (stack), child);

      if (model)
        gtk_menu_section_box_new_toplevel (popover, model, popover->flags);

      g_object_notify (G_OBJECT (popover), "menu-model");
    }
}

/**
 * gtk_popover_menu_get_menu_model:
 * @popover: a #GtkPopoverMenu
 *
 * Returns the menu model used to populate the popover.
 *
 * Returns: (transfer none): the menu model of @popover
 */
GMenuModel *
gtk_popover_menu_get_menu_model (GtkPopoverMenu *popover)
{
  g_return_val_if_fail (GTK_IS_POPOVER_MENU (popover), NULL);

  return popover->model;
}
