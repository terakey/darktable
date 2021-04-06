/*
    This file is part of darktable,
    Copyright (C) 2011-2021 darktable developers.

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

#include "gui/accelerators.h"
#include "common/action.h"
#include "common/darktable.h"
#include "common/debug.h"
#include "common/file_location.h"
#include "common/utility.h"
#include "control/control.h"
#include "develop/blend.h"
#include "gui/presets.h"

#include "bauhaus/bauhaus.h"

#include <assert.h>
#include <gtk/gtk.h>

typedef struct dt_shortcut_t
{
  dt_view_type_flags_t views;

  dt_input_device_t key_device;
  guint key;
  guint mods;
  dt_shortcut_flag_t flags;
  dt_input_device_t move_device;
  dt_shortcut_move_t move;

  dt_action_t *action;

  enum // these will be defined in widget definition block as strings and here indexed
  {
    DT_SHORTCUT_ELEMENT_VALUE = 0,
    DT_SHORTCUT_ELEMENT_SELECTION = 0,
  } element; // this should be index into widget discription structure.
  enum // these will be defined in type definition block as strings and here indexed
  {
    DT_SHORTCUT_EFFECT_DEFAULT_MOVE = -1,
    DT_SHORTCUT_EFFECT_DEFAULT_KEY = 0,
    DT_SHORTCUT_EFFECT_DEFAULT_UP = 1,
    DT_SHORTCUT_EFFECT_DEFAULT_DOWN = 2,

    DT_SHORTCUT_EFFECT_RESET = 0,
    DT_SHORTCUT_EFFECT_UP = 1,
    DT_SHORTCUT_EFFECT_PREVIOUS = 1,
    DT_SHORTCUT_EFFECT_DOWN =2,
    DT_SHORTCUT_EFFECT_NEXT =2,
    DT_SHORTCUT_EFFECT_TOP = 3,
    DT_SHORTCUT_EFFECT_FIRST = 3,
    DT_SHORTCUT_EFFECT_BOTTOM = 4,
    DT_SHORTCUT_EFFECT_LAST = 4,
    DT_SHORTCUT_EFFECT_EDIT = 5,
  } effect;
  float speed;
  int instance; // 0 is from prefs, >0 counting from first, <0 counting from last
} dt_shortcut_t;

enum
{
  DT_SHORTCUT_FLAG_PRESS_MASK    = DT_SHORTCUT_FLAG_PRESS_TRIPLE
                                 | DT_SHORTCUT_FLAG_PRESS_DOUBLE
                                 | DT_SHORTCUT_FLAG_PRESS_LONG,
  DT_SHORTCUT_FLAG_BUTTON_MASK   = DT_SHORTCUT_FLAG_BUTTON_LEFT
                                 | DT_SHORTCUT_FLAG_BUTTON_MIDDLE
                                 | DT_SHORTCUT_FLAG_BUTTON_RIGHT,
  DT_SHORTCUT_FLAG_CLICK_MASK    = DT_SHORTCUT_FLAG_CLICK_TRIPLE
                                 | DT_SHORTCUT_FLAG_CLICK_DOUBLE
                                 | DT_SHORTCUT_FLAG_CLICK_LONG,
  DT_SHORTCUT_FLAG_DIR_MASK      = DT_SHORTCUT_FLAG_DIR_UP
                                 | DT_SHORTCUT_FLAG_DIR_DOWN,
};

const gchar *dt_shortcut_effect_value[]
  = { N_("reset"),
      N_("up"),
      N_("down"),
      N_("top"),
      N_("bottom"),
      N_("edit"),
      NULL };
const gchar *dt_shortcut_effect_selection[]
  = { N_("reset"),
      N_("previous"),
      N_("next"),
      N_("first"),
      N_("last"),
      N_("popup"),
      NULL };
const gchar *dt_shortcut_effect_toggle[]
  = { N_("toggle"),
      N_("on"),
      N_("off"),
      N_("ctrl-toggle"),
      N_("ctrl-on"),
      N_("right-toggle"),
      N_("right-on"),
      NULL };
const gchar *dt_shortcut_effect_activate[]
  = { N_("activate"),
      N_("ctrl-activate"),
      N_("right-activate"),
      NULL };
const gchar *dt_shortcut_effect_instance[]
  = { N_("new"),
      N_("move up"),
      N_("move down"),
      N_("duplicate"),
      N_("delete"),
      N_("rename"),
      NULL };
const gchar *dt_shortcut_effect_presets[]
  = { N_("show"),
      N_("previous"),
      N_("next"),
      N_("store"),
      N_("edit"),
      N_("delete"),
      N_("preferences"),
      NULL };

typedef struct dt_shortcut_element
{
  const gchar *name;
  const gchar **effects;
} dt_shortcut_element;

const dt_shortcut_element dt_shortcut_element_slider[]
  = { { N_("value"), dt_shortcut_effect_value },
      { N_("min"), dt_shortcut_effect_value },
      { N_("max"), dt_shortcut_effect_value },
      { N_("zoom"), dt_shortcut_effect_value },
      { N_("button"), dt_shortcut_effect_toggle },
      { NULL } };
const dt_shortcut_element dt_shortcut_element_combo[]
  = { { N_("selection"), dt_shortcut_effect_selection },
      { N_("button"), dt_shortcut_effect_toggle },
      { NULL } };
const dt_shortcut_element dt_shortcut_element_toggle[]
  = { { N_("button"), dt_shortcut_effect_toggle },
      { NULL } };
const dt_shortcut_element dt_shortcut_element_multivalue[]
  = { { N_("value1"), dt_shortcut_effect_value },
      { N_("value2"), dt_shortcut_effect_value },
      { N_("value3"), dt_shortcut_effect_value },
      { N_("value4"), dt_shortcut_effect_value },
      { NULL } };
const dt_shortcut_element dt_shortcut_element_iop[]
  = { { N_("focus"), dt_shortcut_effect_toggle },
      { N_("enable"), dt_shortcut_effect_toggle },
      { N_("expand"), dt_shortcut_effect_toggle },
      { N_("instance"), dt_shortcut_effect_instance },
      { N_("reset"), dt_shortcut_effect_activate },
      { N_("presets"), dt_shortcut_effect_presets },
      { NULL } };
const dt_shortcut_element dt_shortcut_element_lib[]
  = { { N_("show"), dt_shortcut_effect_toggle },
      { N_("reset"), dt_shortcut_effect_activate },
      { N_("presets"), dt_shortcut_effect_presets },
      { NULL } };

typedef struct dt_device_key_t
{
  dt_input_device_t key_device;
  guint key;
} dt_device_key_t;

typedef struct dt_action_target_t
{
  dt_action_t *action;
  void *target;
} dt_action_target_t;

#define DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE 0

const char *move_string[]
  = { "",
      N_("scroll"),
      N_("pan"),
      N_("horizontal"),
      N_("vertical"),
      N_("diagonal"),
      N_("skew"),
      N_("leftright"),
      N_("updown"),
      N_("pgupdown"),
      NULL };

const struct _modifier_name
{
  GdkModifierType modifier;
  char           *name;
} modifier_string[]
  = { { GDK_SHIFT_MASK  , N_("shift") },
      { GDK_CONTROL_MASK, N_("ctrl" ) },
      { GDK_MOD1_MASK   , N_("alt"  ) },
      { GDK_MOD2_MASK   , N_("cmd"  ) },
      { GDK_SUPER_MASK  , N_("super") },
      { GDK_HYPER_MASK  , N_("hyper") },
      { GDK_META_MASK   , N_("meta" ) },
      { 0, NULL } };

static dt_shortcut_t _sc = { 0 };  //  shortcut under construction

gint shortcut_compare_func(gconstpointer shortcut_a, gconstpointer shortcut_b, gpointer user_data)
{
  const dt_shortcut_t *a = (const dt_shortcut_t *)shortcut_a;
  const dt_shortcut_t *b = (const dt_shortcut_t *)shortcut_b;

  dt_view_type_flags_t active_view = GPOINTER_TO_INT(user_data);
  int a_in_view = a->views ? a->views & active_view : -1; // put fallbacks last
  int b_in_view = b->views ? b->views & active_view : -1; // put fallbacks last

// FIXME if no views then this is fallback; sort by action first (after putting all fallbacks last)

  if(a_in_view != b_in_view)
    return b_in_view - a_in_view; // reverse order; in current view first
  if(a->key_device != b->key_device)
    return a->key_device - b->key_device;
  if(a->key != b->key)
    return a->key - b->key;
  if((a->flags ^ b->flags) & ~DT_SHORTCUT_FLAG_DIR_MASK)
    return a->flags - b->flags;
  if(a->move_device != b->move_device)
    return a->move_device - b->move_device;
  if(a->move != b->move)
    return a->move - b->move;
  if(a->mods != b->mods)
    return a->mods - b->mods;
  if(!(a->flags ^ b->flags ^ DT_SHORTCUT_FLAG_DIR_MASK)) // Only not matched if one has up, other has down
    return a->flags - b->flags;

  return 0;
};

static gchar *_action_full_label(dt_action_t *action)
{
  if(action->owner)
  {
    gchar *owner_label = _action_full_label(action->owner);
    gchar *full_label = g_strdup_printf("%s/%s", owner_label, action->label);
    g_free(owner_label);
    return full_label;
  }
  else
    return g_strdup(action->label);
}

static gchar *_action_full_label_translated(dt_action_t *action)
{
  if(action->owner)
  {
    gchar *owner_label = _action_full_label_translated(action->owner);
    gchar *full_label = g_strdup_printf("%s/%s", owner_label, action->label_translated);
    g_free(owner_label);
    return full_label;
  }
  else
    return g_strdup(action->label_translated);
}

static void _dump_actions(FILE *f, dt_action_t *action)
{
  while(action)
  {
    gchar *label = _action_full_label(action);
    fprintf(f, "%s %s\n", label, !action->target ? "*" : "");
    g_free(label);
    if(action->type <= DT_ACTION_TYPE_SECTION)
      _dump_actions(f, action->target);
    action = action->next;
  }
}

dt_input_device_t dt_register_input_driver(dt_lib_module_t *module, const dt_input_driver_definition_t *callbacks)
{
  dt_input_device_t id = 10;

  GSList *driver = darktable.control->input_drivers;
  while(driver)
  {
    if(((dt_input_driver_definition_t *)driver->data)->module == module) return id;
    driver = driver->next;
    id += 10;
  }

  dt_input_driver_definition_t *new_driver = calloc(1, sizeof(dt_input_driver_definition_t));
  *new_driver = *callbacks;
  new_driver->module = module;
  darktable.control->input_drivers = g_slist_append(darktable.control->input_drivers, (gpointer)new_driver);

  return id;
}

#define DT_MOVE_NAME -1
static gchar *_shortcut_key_move_name(dt_input_device_t id, guint key_or_move, guint mods, gboolean display)
{
  gchar *name = NULL, *post_name = NULL;
  if(id == DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE)
  {
    if(mods == DT_MOVE_NAME)
      return g_strdup(display && key_or_move != 0 ? _(move_string[key_or_move]) : move_string[key_or_move]);
    else
    {
      if(display)
      {
        gchar *key_name = gtk_accelerator_get_label(key_or_move, 0);
        post_name = g_utf8_strdown(key_name, -1);
        g_free(key_name);
      }
      else
        name = key_or_move ? gtk_accelerator_name(key_or_move, 0) : g_strdup("None");
    }
  }
  else
  {
    GSList *driver = darktable.control->input_drivers;
    while(driver)
    {
      if((id -= 10) < 10)
      {
        dt_input_driver_definition_t *callbacks = driver->data;
        gchar *without_device
          = mods == DT_MOVE_NAME
          ? callbacks->move_to_string(key_or_move, display)
          : callbacks->key_to_string(key_or_move, display);

        if(display)
          post_name = without_device;
        else
        {
          char id_str[2] = "\0\0";
          if(id) id_str[0] = '0' + id;

          name = g_strdup_printf("%s%s:%s", callbacks->name, id_str, without_device);
          g_free(without_device);
        }
        break;
      }
      driver = driver->next;
    }

    if(!driver) name = g_strdup(_("Unknown driver"));
  }
  if(mods != DT_MOVE_NAME)
  {
    for(const struct _modifier_name *mod_str = modifier_string;
        mod_str->modifier;
        mod_str++)
    {
      if(mods & mod_str->modifier)
      {
        gchar *save_name = name;
        name = display
             ? g_strdup_printf("%s%s+", name ? name : "", _(mod_str->name))
             : g_strdup_printf("%s;%s", name ? name : "",   mod_str->name);
        g_free(save_name);
      }
    }
  }

  if(post_name)
  {
    gchar *save_name = name;
    name = g_strdup_printf("%s%s", name ? name : "", post_name);
    g_free(save_name);
    g_free(post_name);
  }

  return name;
}

static gboolean _shortcut_is_move(dt_shortcut_t *s)
{
  return (s->move_device != 0 || s->move != 0) && !(s->flags & DT_SHORTCUT_FLAG_DIR_MASK);
}

static gchar *_shortcut_description(dt_shortcut_t *s, gboolean full)
{
  static gchar hint[1024];
  int length = 0;

#define add_hint(format, ...) length += length >= sizeof(hint) ? 0 : snprintf(hint + length, sizeof(hint) - length, format, ##__VA_ARGS__)

  gchar *key_name = _shortcut_key_move_name(s->key_device, s->key, s->mods, TRUE);
  gchar *move_name = _shortcut_key_move_name(s->move_device, s->move, DT_MOVE_NAME, TRUE);

  add_hint("%s%s", key_name, s->key_device || s->key ? "" : move_name);

  if(s->flags & DT_SHORTCUT_FLAG_PRESS_DOUBLE ) add_hint(" %s", _("double"));
  if(s->flags & DT_SHORTCUT_FLAG_PRESS_TRIPLE ) add_hint(" %s", _("triple"));
  if(s->flags & DT_SHORTCUT_FLAG_PRESS_LONG   ) add_hint(" %s", _("long"));
  if(s->flags & DT_SHORTCUT_FLAG_PRESS_MASK   ) add_hint(" %s", _("press"));
  if(s->flags & DT_SHORTCUT_FLAG_BUTTON_MASK  ) add_hint(",");
  if(s->flags & DT_SHORTCUT_FLAG_BUTTON_LEFT  ) add_hint(" %s", _("left"));
  if(s->flags & DT_SHORTCUT_FLAG_BUTTON_RIGHT ) add_hint(" %s", _("right"));
  if(s->flags & DT_SHORTCUT_FLAG_BUTTON_MIDDLE) add_hint(" %s", _("middle"));
  if(s->flags & DT_SHORTCUT_FLAG_CLICK_DOUBLE ) add_hint(" %s", _("double"));
  if(s->flags & DT_SHORTCUT_FLAG_CLICK_TRIPLE ) add_hint(" %s", _("triple"));
  if(s->flags & DT_SHORTCUT_FLAG_CLICK_LONG   ) add_hint(" %s", _("long"));
  if(s->flags & DT_SHORTCUT_FLAG_CLICK_MASK  ||
     s->flags & DT_SHORTCUT_FLAG_BUTTON_MASK  ) add_hint(" %s", _("click"));

  if(*move_name && (s->key_device || s->key))
  {
    add_hint(", %s", move_name);
    if(s->flags & DT_SHORTCUT_FLAG_DIR_MASK)
      add_hint(", %s", s->flags & DT_SHORTCUT_FLAG_DIR_UP ? _("up") : _("down"));
  }

  g_free(key_name);
  g_free(move_name);

  if(full)
  {
    if(s->instance == 1) add_hint(", %s", _("first instance"));
    else
    if(s->instance == -1) add_hint(", %s", _("last instance"));
    else
    if(s->instance != 0) add_hint(", %s %+d", _("relative instance"), s->instance);

    if(s->speed != 1.0) add_hint(_(", %s *%g"), _("speed"), s->speed);
  }

#undef add_hint

  return hint;
}

static void _insert_shortcut_in_list(GHashTable *ht, char *shortcut, dt_action_t *ac, char *label)
{
  if(ac->owner && ac->owner->owner)
    _insert_shortcut_in_list(ht, shortcut, ac->owner, g_strdup_printf("%s/%s", ac->owner->label_translated, label));
  {
    GtkListStore *list_store = g_hash_table_lookup(ht, ac->owner);
    if(!list_store)
    {
      list_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
      g_hash_table_insert(ht, ac->owner, list_store);
    }

    gtk_list_store_insert_with_values(list_store, NULL, -1, 0, shortcut, 1, label, -1);
  }

  g_free(label);
}

GHashTable *dt_shortcut_category_lists(dt_view_type_flags_t v)
{
  GHashTable *ht = g_hash_table_new(NULL, NULL);

  for(GSequenceIter *iter = g_sequence_get_begin_iter(darktable.control->shortcuts);
      !g_sequence_iter_is_end(iter);
      iter = g_sequence_iter_next(iter))
  {
    dt_shortcut_t *s = g_sequence_get(iter);
    if(s && s->views & v)
      _insert_shortcut_in_list(ht, _shortcut_description(s, TRUE), s->action, g_strdup(s->action->label_translated));
  }

  return ht;
}

static gboolean _shortcut_tooltip_callback(GtkWidget *widget, gint x, gint y, gboolean keyboard_mode,
                                           GtkTooltip *tooltip, gpointer user_data)
{
  gchar *description = NULL;
  dt_action_t *action = NULL;

  if(GTK_IS_TREE_VIEW(widget))
  {
    GtkTreePath *path = NULL;
    GtkTreeModel *model;
    GtkTreeIter iter;
    if(!gtk_tree_view_get_tooltip_context(GTK_TREE_VIEW(widget), &x, &y, keyboard_mode, &model, &path, &iter))
      return FALSE;

    gtk_tree_model_get(model, &iter, 0, &action, -1);
    gtk_tree_view_set_tooltip_row(GTK_TREE_VIEW(widget), tooltip, path);
    gtk_tree_path_free(path);
  }
  else
    action = g_hash_table_lookup(darktable.control->widgets, widget);

  for(GSequenceIter *iter = g_sequence_get_begin_iter(darktable.control->shortcuts);
      !g_sequence_iter_is_end(iter);
      iter = g_sequence_iter_next(iter))
  {
    dt_shortcut_t *s = g_sequence_get(iter);
    if(s->action == action)
    {
      gchar *old_description = description;
      description = g_strdup_printf("%s\n%s", description ? description : "", _shortcut_description(s, TRUE));
      g_free(old_description);
    }
  }

  if(description)
  {
    gchar *original_markup = gtk_widget_get_tooltip_markup(widget);
    gchar *desc_escaped = g_markup_escape_text(description, -1);
    gchar *markup_text = g_strdup_printf("%s<span style='italic' foreground='red'>%s</span>",
                                         original_markup ? original_markup : "Shortcuts:", desc_escaped);
    gtk_tooltip_set_markup(tooltip, markup_text);
    g_free(original_markup);
    g_free(desc_escaped);
    g_free(markup_text);
    g_free(description);

    return TRUE;
  }

  return FALSE;
}

void find_views(dt_shortcut_t *s)
{
  s->views = 0;

  dt_action_t *owner = s->action->owner;
  while(owner && owner->type == DT_ACTION_TYPE_SECTION) owner = owner->owner;
  if(owner)
  switch(owner->type)
  {
  case DT_ACTION_TYPE_IOP:
    s->views = DT_VIEW_DARKROOM;
    break;
  case DT_ACTION_TYPE_VIEW:
    {
      dt_view_t *view = (dt_view_t *)owner;

      s->views = view->view(view);
    }
    break;
  case DT_ACTION_TYPE_LIB:
    {
      dt_lib_module_t *lib = (dt_lib_module_t *)owner;

      const gchar **views = lib->views(lib);
      while (*views)
      {
        if     (strcmp(*views, "lighttable") == 0)
          s->views |= DT_VIEW_LIGHTTABLE;
        else if(strcmp(*views, "darkroom") == 0)
          s->views |= DT_VIEW_DARKROOM;
        else if(strcmp(*views, "print") == 0)
          s->views |= DT_VIEW_PRINT;
        else if(strcmp(*views, "slideshow") == 0)
          s->views |= DT_VIEW_SLIDESHOW;
        else if(strcmp(*views, "map") == 0)
          s->views |= DT_VIEW_MAP;
        else if(strcmp(*views, "tethering") == 0)
          s->views |= DT_VIEW_TETHERING;
        else if(strcmp(*views, "*") == 0)
          s->views |= DT_VIEW_DARKROOM | DT_VIEW_LIGHTTABLE | DT_VIEW_TETHERING |
                      DT_VIEW_MAP | DT_VIEW_PRINT | DT_VIEW_SLIDESHOW;
        views++;
      }
    }
    break;
  case DT_ACTION_TYPE_CATEGORY:
    if(owner == &darktable.control->actions_blend)
      s->views = DT_VIEW_DARKROOM;
    else if(owner == &darktable.control->actions_lua)
      s->views = DT_VIEW_DARKROOM | DT_VIEW_LIGHTTABLE | DT_VIEW_TETHERING |
                 DT_VIEW_MAP | DT_VIEW_PRINT | DT_VIEW_SLIDESHOW;
    else if(owner == &darktable.control->actions_thumb)
    {
      s->views = DT_VIEW_DARKROOM | DT_VIEW_MAP | DT_VIEW_TETHERING | DT_VIEW_PRINT;
      if(!strstr(s->action->label,"history"))
        s->views |= DT_VIEW_LIGHTTABLE; // lighttable has copy/paste history shortcuts in separate lib
    }
    else
      fprintf(stderr, "[find_views] views for category '%s' unknown\n", owner->label);
    break;
  case DT_ACTION_TYPE_GLOBAL:
    s->views = DT_VIEW_DARKROOM | DT_VIEW_LIGHTTABLE | DT_VIEW_TETHERING |
               DT_VIEW_MAP | DT_VIEW_PRINT | DT_VIEW_SLIDESHOW;
    break;
  default:
    break;
  }
}

static GtkTreeStore *shortcuts_store = NULL;
static GtkTreeStore *actions_store = NULL;
static GtkWidget *grab_widget = NULL, *grab_window = NULL;

#define NUM_CATEGORIES 3
const gchar *category_label[NUM_CATEGORIES]
  = { N_("active view"),
      N_("other views"),
      N_("fallbacks (not implemented)") };

static void shortcuts_store_category(GtkTreeIter *category, dt_shortcut_t *s, dt_view_type_flags_t view)
{
  gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(shortcuts_store), category, NULL,
                                s && s->views ? s->views & view ? 0 : 1 : 2);
}

gboolean remove_from_store(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
  gpointer iter_data;
  gtk_tree_model_get(model, iter, 0, &iter_data, -1);
  if(iter_data == data)
  {
    gtk_tree_store_remove(GTK_TREE_STORE(model), iter);
    return TRUE;
  }

  return FALSE;
}

static void remove_shortcut(GSequenceIter *shortcut)
{
  if(shortcuts_store)
    gtk_tree_model_foreach(GTK_TREE_MODEL(shortcuts_store), remove_from_store, shortcut);

  dt_shortcut_t *s = g_sequence_get(shortcut);
  if(s && s->flags & DT_SHORTCUT_FLAG_DIR_MASK) // was this a split move?
  {
    // unsplit the other half of the move
    s->flags &= ~DT_SHORTCUT_FLAG_DIR_MASK;
    dt_shortcut_t *o = g_sequence_get(g_sequence_iter_prev(shortcut));
    if(g_sequence_iter_is_begin(shortcut) || shortcut_compare_func(s, o, GINT_TO_POINTER(s->views)))
      o = g_sequence_get(g_sequence_iter_next(shortcut));
    o->flags &= ~DT_SHORTCUT_FLAG_DIR_MASK;
  }

  g_sequence_remove(shortcut);
}

static void add_shortcut(dt_shortcut_t *shortcut, dt_view_type_flags_t view)
{
  GSequenceIter *new_shortcut = g_sequence_insert_sorted(darktable.control->shortcuts, shortcut,
                                                         shortcut_compare_func, GINT_TO_POINTER(view));

  GtkTreeModel *model = GTK_TREE_MODEL(shortcuts_store);
  if(model)
  {
    GSequenceIter *prev_shortcut = g_sequence_iter_prev(new_shortcut);
    GSequenceIter *seq_iter = NULL;
    GtkTreeIter category, child;
    shortcuts_store_category(&category, shortcut, view);

    gint position = 1, found = 0;
    if(gtk_tree_model_iter_children(model, &child, &category))
    do
    {
      gtk_tree_model_get(model, &child, 0, &seq_iter, -1);
      if(seq_iter == prev_shortcut)
      {
        found = position;
        break;
      }
      position++;
    } while(gtk_tree_model_iter_next(model, &child));

    gtk_tree_store_insert_with_values(shortcuts_store, NULL, &category, found, 0, new_shortcut, -1);
  }

  if(shortcut->action && shortcut->action->type == DT_ACTION_TYPE_KEY_PRESSED && shortcut->action->target)
  {
    GtkAccelKey *key = shortcut->action->target;
    key->accel_key = shortcut->key;
    key->accel_mods = shortcut->mods;
  }
}

static void _shortcut_row_inserted(GtkTreeModel *tree_model, GtkTreePath *path, GtkTreeIter *iter, gpointer view)
{
  // connect to original store, not filtered one, because otherwise view not sufficiently updated to expand

  GtkTreePath *filter_path = gtk_tree_model_filter_convert_child_path_to_path
                             (GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(view)), path);
  if(!filter_path) return;

  gtk_tree_view_expand_to_path(view, filter_path);
  gtk_tree_view_scroll_to_cell(view, filter_path, NULL, TRUE, 0.5, 0);
  gtk_tree_view_set_cursor(view, filter_path, NULL, FALSE);
  gtk_tree_path_free(filter_path);
}

static gboolean insert_shortcut(dt_shortcut_t *shortcut, gboolean confirm)
{
  if(shortcut->action && shortcut->action && shortcut->action->type == DT_ACTION_TYPE_KEY_PRESSED &&
     (shortcut->key_device != DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE ||
      shortcut->move_device != DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE || shortcut->move != DT_SHORTCUT_MOVE_NONE ||
      shortcut->flags & (DT_SHORTCUT_FLAG_PRESS_MASK | DT_SHORTCUT_FLAG_BUTTON_MASK)))
  {
    fprintf(stderr, "[insert_shortcut] only key+mods type shortcut supported for key_pressed style accelerators\n");
    dt_control_log(_("only key + ctrl/shift/alt supported for this shortcut"));
    return FALSE;
  }
  // FIXME: prevent multiple shortcuts because only the last one will work.
  // better solution; incorporate these special case accelerators into standard shortcut framework

  dt_shortcut_t *s = calloc(sizeof(dt_shortcut_t), 1);
  *s = *shortcut;
  find_views(s);
  dt_view_type_flags_t real_views = s->views;

  const dt_view_t *vw = NULL;
  if(darktable.view_manager) vw = dt_view_manager_get_current_view(darktable.view_manager);
  dt_view_type_flags_t view = vw && vw->view ? vw->view(vw) : DT_VIEW_LIGHTTABLE;

  // check (and remove if confirmed) clashes in current and other views
  gboolean remove_existing = !confirm;
  do
  {
    gchar *existing_labels = NULL;
    int active_view = 1;
    do
    {
      GSequenceIter *existing = g_sequence_lookup(darktable.control->shortcuts, s, shortcut_compare_func, GINT_TO_POINTER(view));
      if(existing) // at least one found
      {
        // go to first one that has same shortcut
        while(!g_sequence_iter_is_begin(existing) &&
              !shortcut_compare_func(s, g_sequence_get(g_sequence_iter_prev(existing)), GINT_TO_POINTER(view)))
          existing = g_sequence_iter_prev(existing);

        do
        {
          GSequenceIter *saved_next = g_sequence_iter_next(existing);

          dt_shortcut_t *e = g_sequence_get(existing);

          if(e->action == s->action)
          {
            gchar *question = NULL;
            if(_shortcut_is_move(e) && e->effect != DT_SHORTCUT_EFFECT_DEFAULT_MOVE)
            {
              question = g_markup_printf_escaped("\n%s\n", _("create separate shortcuts for up and down move?"));
              if(!confirm ||
                 dt_gui_show_standalone_yes_no_dialog(_("move shortcut exists with single effect"), question, _("no"), _("yes")))
              {
                e->flags |= s->flags & DT_SHORTCUT_FLAG_DIR_UP ? DT_SHORTCUT_FLAG_DIR_DOWN : DT_SHORTCUT_FLAG_DIR_UP;
                if(s->effect == DT_SHORTCUT_EFFECT_DEFAULT_MOVE)
                  s->effect = DT_SHORTCUT_EFFECT_DEFAULT_KEY;
                add_shortcut(s, view);
                g_free(question);
                return TRUE;
              }
            }
            else if(e->element  != s->element ||
                    e->effect   != s->effect  ||
                    e->speed    != s->speed   ||
                    e->instance != s->instance )
            {
              question = g_markup_printf_escaped("\n%s\n", _("reset the settings of the shortcut?"));
              if(!confirm ||
                 dt_gui_show_standalone_yes_no_dialog(_("shortcut exists with different settings"), question, _("no"), _("yes")))
              {
                e->element  = s->element;
                e->effect   = s->effect;
                e->speed    = s->speed;
                e->instance = s->instance;
              }
            }
            else
            {
              // there should be no other clashes because same mapping already existed
              question = g_markup_printf_escaped("\n%s\n", _("remove the shortcut?"));
              if(confirm &&
                 dt_gui_show_standalone_yes_no_dialog(_("shortcut already exists"), question, _("no"), _("yes")))
              {
                remove_shortcut(existing);
              }
            }
            g_free(question);
            g_free(s);
            return FALSE;
          }

          if(e->views & real_views) // overlap
          {
            if(remove_existing)
              remove_shortcut(existing);
            else
            {
              gchar *old_labels = existing_labels;
              gchar *new_label = _action_full_label_translated(e->action);
              existing_labels = g_strdup_printf("%s\n%s",
                                                existing_labels ? existing_labels : "",
                                                new_label);
              g_free(new_label);
              g_free(old_labels);
            }
          }

          existing = saved_next;
        } while(!g_sequence_iter_is_end(existing) && !shortcut_compare_func(s, g_sequence_get(existing), GINT_TO_POINTER(view)));
      }

      s->views ^= view; // look in the opposite selection
    } while(active_view--);

    if(existing_labels)
    {
      gchar *question = g_markup_printf_escaped("\n%s\n<i>%s</i>\n",
                                                _("remove these existing shortcuts?"),
                                                existing_labels);
      remove_existing = dt_gui_show_standalone_yes_no_dialog(_("clashing shortcuts exist"),
                                                             question, _("no"), _("yes"));

      g_free(existing_labels);
      g_free(question);

      if(!remove_existing)
      {
        g_free(s);
        return FALSE;
      }
    }
    else
    {
      remove_existing = FALSE;
    }

  } while(remove_existing);

  s->flags &= ~DT_SHORTCUT_FLAG_DIR_MASK;
  add_shortcut(s, view);

  shortcut->flags = s->flags;
  return TRUE;
}

static const dt_shortcut_element *_action_find_elements(dt_action_t *action)
{
  if(!action) return NULL;

  switch(action->type)
  {
  case DT_ACTION_TYPE_SLIDER:
    return dt_shortcut_element_slider;
  case DT_ACTION_TYPE_COMBO:
    return dt_shortcut_element_combo;
  case DT_ACTION_TYPE_TOGGLE:
    return dt_shortcut_element_toggle;
  case DT_ACTION_TYPE_IOP:
    return dt_shortcut_element_iop;
  case DT_ACTION_TYPE_LIB:
    return dt_shortcut_element_lib;
  default:
    return NULL;
  }
}

typedef enum
{
  SHORTCUT_VIEW_DESCRIPTION,
  SHORTCUT_VIEW_ACTION,
  SHORTCUT_VIEW_ELEMENT,
  SHORTCUT_VIEW_EFFECT,
  SHORTCUT_VIEW_SPEED,
  SHORTCUT_VIEW_INSTANCE,
  SHORTCUT_VIEW_COLUMNS
} field_id;

#define NUM_INSTANCES 5 // or 3, but change char relative[] = "-2" to "-1"
const gchar *instance_label[/*NUM_INSTANCES*/]
  = { N_("preferred"),
      N_("first"),
      N_("last"),
      N_("second"),
      N_("last but one") };

static void _fill_tree_fields(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
  void *data_ptr = NULL;
  gtk_tree_model_get(model, iter, 0, &data_ptr, -1);
  field_id field = GPOINTER_TO_INT(data);
  gchar *field_text = NULL;
  const dt_shortcut_element *elements = NULL;
  gboolean editable = FALSE;
  PangoUnderline underline = PANGO_UNDERLINE_NONE;
  if(GPOINTER_TO_UINT(data_ptr) < NUM_CATEGORIES)
  {
    if(field == SHORTCUT_VIEW_DESCRIPTION)
    {
      field_text = g_strdup(_(category_label[GPOINTER_TO_INT(data_ptr)]));
    }
    else
    {
      field_text = g_strdup("");
    }
  }
  else
  {
    dt_shortcut_t *s = g_sequence_get(data_ptr);
    switch(field)
    {
    case SHORTCUT_VIEW_DESCRIPTION:
      field_text = g_strdup(_shortcut_description(s, FALSE));
      break;
    case SHORTCUT_VIEW_ACTION:
      if(s->action)
      {
        field_text = _action_full_label_translated(s->action);
        if(s->action->type == DT_ACTION_TYPE_KEY_PRESSED)
          underline = PANGO_UNDERLINE_ERROR;
      }
      break;
    case SHORTCUT_VIEW_ELEMENT:
      elements = _action_find_elements(s->action);
      if(elements)
      {
        field_text = g_strdup(elements[s->element].name);
        editable = TRUE;
      }
      break;
    case SHORTCUT_VIEW_EFFECT:
      elements = _action_find_elements(s->action);
      if(elements)
      {
        if(s->effect >= 0)
          field_text = g_strdup(elements[s->element].effects[s->effect]);
        editable = TRUE;
      }
      break;
    case SHORTCUT_VIEW_SPEED:
      elements = _action_find_elements(s->action);
      if(elements && elements[s->element].effects == dt_shortcut_effect_value &&
         (s->effect == DT_SHORTCUT_EFFECT_DEFAULT_MOVE ||
          s->effect == DT_SHORTCUT_EFFECT_DEFAULT_UP ||
          s->effect == DT_SHORTCUT_EFFECT_DEFAULT_DOWN))
      {
        field_text = g_strdup_printf("%.3f", s->speed);
        editable = TRUE;
      }
      break;
    case SHORTCUT_VIEW_INSTANCE:
      if(s->action)
      for(dt_action_t *owner = s->action->owner; owner; owner = owner->owner)
      {
        if(owner->type == DT_ACTION_TYPE_IOP)
        {
          dt_iop_module_so_t *iop = (dt_iop_module_so_t *)owner;

          if(!(iop->flags() & IOP_FLAGS_ONE_INSTANCE))
          {
            field_text = abs(s->instance) <= (NUM_INSTANCES - 1) /2
                       ? g_strdup(_(instance_label[abs(s->instance)*2 - (s->instance > 0)]))
                       : g_strdup_printf("%+d", s->instance);
            editable = TRUE;
          }
        }
      }
      break;
    default:
      break;
    }
  }
  g_object_set(cell, "text", field_text, "editable", editable, "underline", underline, NULL);
  g_free(field_text);
}

static void _add_prefs_column(GtkTreeView *tree, GtkCellRenderer *renderer, char *name, int position)
{
  GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(name, renderer, NULL);
  gtk_tree_view_column_set_cell_data_func(column, renderer, _fill_tree_fields, GINT_TO_POINTER(position), NULL);
  gtk_tree_view_column_set_resizable(column, TRUE);
  gtk_tree_view_append_column(tree, column);
}

static dt_shortcut_t *find_edited_shortcut(GtkTreeModel *model, const gchar *path_string)
{
  GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
  GtkTreeIter iter;
  gtk_tree_model_get_iter(model, &iter, path);
  gtk_tree_path_free(path);

  void *data_ptr = NULL;
  gtk_tree_model_get(model, &iter, 0, &data_ptr, -1);

  return g_sequence_get(data_ptr);
}

static void _element_editing_started(GtkCellRenderer *renderer, GtkCellEditable *editable, char *path, gpointer data)
{
  dt_shortcut_t *s = find_edited_shortcut(data, path);

  GtkComboBox *combo_box = GTK_COMBO_BOX(editable);
  GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model(combo_box));
  gtk_list_store_clear(store);

  for(const dt_shortcut_element *element = _action_find_elements(s->action); element && element->name ; element++)
    gtk_list_store_insert_with_values(store, NULL, -1, 0, _(element->name), -1);
}

static void _element_changed(GtkCellRendererCombo *combo, char *path_string, GtkTreeIter *new_iter, gpointer data)
{
  dt_shortcut_t *s = find_edited_shortcut(data, path_string);

  GtkTreeModel *model = NULL;
  g_object_get(combo, "model", &model, NULL);
  GtkTreePath *path = gtk_tree_model_get_path(model, new_iter);
  gint new_index = gtk_tree_path_get_indices(path)[0];
  gtk_tree_path_free(path);

  const dt_shortcut_element *elements = _action_find_elements(s->action);
  if(elements[s->element].effects != elements[new_index].effects)
  {
    s->effect = 0; // FIXME default? for move? internally effect = -1 is move and value up/down. when selecting either up or down, move back to -1.
  }
  s->element = new_index;

  dt_shortcuts_save(FALSE);
}

static void _effect_editing_started(GtkCellRenderer *renderer, GtkCellEditable *editable, char *path, gpointer data)
{
  dt_shortcut_t *s = find_edited_shortcut(data, path);

  GtkComboBox *combo_box = GTK_COMBO_BOX(editable);
  GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model(combo_box));
  gtk_list_store_clear(store);

  const dt_shortcut_element *elements = _action_find_elements(s->action);
  if(elements)
    for(const gchar **effect = elements[s->element].effects; *effect ; effect++)
      gtk_list_store_insert_with_values(store, NULL, -1, 0, _(*effect), -1);
}

static void _effect_changed(GtkCellRendererCombo *combo, char *path_string, GtkTreeIter *new_iter, gpointer data)
{
  dt_shortcut_t *s = find_edited_shortcut(data, path_string);

  GtkTreeModel *model = NULL;
  g_object_get(combo, "model", &model, NULL);
  GtkTreePath *path = gtk_tree_model_get_path(model, new_iter);
  gint new_index = s->effect = gtk_tree_path_get_indices(path)[0];
  gtk_tree_path_free(path);

  if(_shortcut_is_move(s) &&
     (new_index == DT_SHORTCUT_EFFECT_DEFAULT_UP || new_index == DT_SHORTCUT_EFFECT_DEFAULT_DOWN))
    s->effect = DT_SHORTCUT_EFFECT_DEFAULT_MOVE;
  else
    s->effect = new_index;

  dt_shortcuts_save(FALSE);
}

static void _speed_edited(GtkCellRendererText *cell, const gchar *path_string, const gchar *new_text, gpointer data)
{
  find_edited_shortcut(data, path_string)->speed = atof(new_text);

  dt_shortcuts_save(FALSE);
}

static void _instance_edited(GtkCellRendererText *cell, const gchar *path_string, const gchar *new_text, gpointer data)
{
  dt_shortcut_t *s = find_edited_shortcut(data, path_string);

  if(!(s->instance = atoi(new_text)))
    for(int i = 0; i < NUM_INSTANCES; i++)
      if(!strcmp(instance_label[i], new_text))
        s->instance = (i + 1) / 2 * (i % 2 ? 1 : -1);

  dt_shortcuts_save(FALSE);
}

static void grab_in_tree_view(GtkTreeView *tree_view)
{
  g_set_weak_pointer(&grab_widget, gtk_widget_get_parent(gtk_widget_get_parent(GTK_WIDGET(tree_view)))); // static
  gtk_widget_set_sensitive(grab_widget, FALSE);
  g_set_weak_pointer(&grab_window, gtk_widget_get_toplevel(grab_widget));
  g_signal_connect(grab_window, "event", G_CALLBACK(dt_shortcut_dispatcher), NULL);
}

static void ungrab_grab_widget()
{
  gdk_seat_ungrab(gdk_display_get_default_seat(gdk_display_get_default()));

  if(grab_widget)
  {
    gtk_widget_set_sensitive(grab_widget, TRUE);
    g_signal_handlers_disconnect_by_func(gtk_widget_get_toplevel(grab_widget), G_CALLBACK(dt_shortcut_dispatcher), NULL);
    grab_widget = NULL;
  }
}

static void _shortcut_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data)
{
  GtkTreeIter iter;
  gtk_tree_model_get_iter(GTK_TREE_MODEL(user_data), &iter, path);

  GSequenceIter  *shortcut_iter = NULL;
  gtk_tree_model_get(GTK_TREE_MODEL(user_data), &iter, 0, &shortcut_iter, -1);

  if(GPOINTER_TO_UINT(shortcut_iter) < NUM_CATEGORIES) return;

  dt_shortcut_t *s = g_sequence_get(shortcut_iter);
  _sc.action = s->action;
  _sc.instance = s->instance;

  grab_in_tree_view(tree_view);
}

static gboolean _shortcut_key_pressed(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
  if(event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_BackSpace || event->keyval == GDK_KEY_KP_Delete)
  {
    GtkTreeView *view = GTK_TREE_VIEW(widget);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(view);

    GtkTreeIter iter;
    GtkTreeModel *model = NULL;
    if(gtk_tree_selection_get_selected(selection, &model, &iter))
    {
      GSequenceIter  *shortcut_iter = NULL;
      gtk_tree_model_get(model, &iter, 0, &shortcut_iter, -1);

      if(GPOINTER_TO_UINT(shortcut_iter) >= NUM_CATEGORIES)
      {
        gchar *question = g_markup_printf_escaped("\n%s\n", _("remove the selected shortcut?"));
        if(dt_gui_show_standalone_yes_no_dialog(_("removing shortcut"), question, _("no"), _("yes")))
        {
          remove_shortcut(shortcut_iter);

          dt_shortcuts_save(FALSE);
        }
        g_free(question);
      }
    }

    return TRUE;
  }

  return FALSE;
}

static gboolean _add_actions_to_tree(GtkTreeIter *parent, dt_action_t *action,
                                     dt_action_t *find, GtkTreeIter *found)
{
  gboolean any_leaves = FALSE;

  GtkTreeIter iter;
  while(action)
  {
    gtk_tree_store_insert_with_values(actions_store, &iter, parent, -1, 0, action, -1);

    if(action->type <= DT_ACTION_TYPE_SECTION &&
       !_add_actions_to_tree(&iter, action->target, find, found))
      gtk_tree_store_remove(actions_store, &iter);
    else
    {
      any_leaves = TRUE;
      if(action == find) *found = iter;
    }

    action = action->next;
  }

  return any_leaves;
}

static void _show_action_label(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
  dt_action_t *action = NULL;
  gtk_tree_model_get(model, iter, 0, &action, -1);
  g_object_set(cell, "text", action->label_translated, NULL);
}

static void _action_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data)
{
  GtkTreeIter iter;
  gtk_tree_model_get_iter(GTK_TREE_MODEL(user_data), &iter, path);

  gtk_tree_model_get(GTK_TREE_MODEL(user_data), &iter, 0, &_sc.action, -1);
  _sc.instance = 0;

  if(_sc.action->type != DT_ACTION_TYPE_CATEGORY && _sc.action->type != DT_ACTION_TYPE_SECTION && _sc.action->type != DT_ACTION_TYPE_GLOBAL)
    grab_in_tree_view(tree_view);
}

gboolean shortcut_selection_function(GtkTreeSelection *selection,
                                     GtkTreeModel *model, GtkTreePath *path,
                                     gboolean path_currently_selected, gpointer data)
{
  GtkTreeIter iter;
  gtk_tree_model_get_iter(model, &iter, path);

  void *data_ptr = NULL;
  gtk_tree_model_get(model, &iter, 0, &data_ptr, -1);

  if(GPOINTER_TO_UINT(data_ptr) < NUM_CATEGORIES)
  {
    GtkTreeView *view = gtk_tree_selection_get_tree_view(selection);
    if(gtk_tree_view_row_expanded(view, path))
      gtk_tree_view_collapse_row(view, path);
    else
      gtk_tree_view_expand_row(view, path, FALSE);

    return FALSE;
  }
  else
    return TRUE;
}

static dt_action_t *_selected_action = NULL;

static gboolean _action_view_click(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  GtkTreeView *view = GTK_TREE_VIEW(widget);

  if(event->button == GDK_BUTTON_PRIMARY)
  {
    GtkTreeSelection *selection = gtk_tree_view_get_selection(view);

    GtkTreePath *path = NULL;
    if(gtk_tree_view_get_path_at_pos(view, (gint)event->x, (gint)event->y, &path, NULL, NULL, NULL))
    {
      if(event->type == GDK_DOUBLE_BUTTON_PRESS)
      {
        gtk_tree_selection_select_path(selection, path);
        _action_row_activated(view, path, NULL, gtk_tree_view_get_model(view));
      }
      else if(gtk_tree_selection_path_is_selected(selection, path))
      {
        gtk_tree_selection_unselect_path(selection, path);
        gtk_tree_view_collapse_row(view, path);

        return TRUE;
      }
    }
    else
      gtk_tree_selection_unselect_all(selection);
  }

  return FALSE;
}

static gboolean _action_view_map(GtkTreeView *view, GdkEvent *event, gpointer found_iter)
{
  GtkTreePath *path = gtk_tree_model_get_path(gtk_tree_view_get_model(view), found_iter);
  gtk_tree_view_expand_to_path(view, path);
  gtk_tree_view_scroll_to_cell(view, path, NULL, TRUE, 0.5, 0);
  gtk_tree_path_free(path);

  gtk_tree_selection_select_iter(gtk_tree_view_get_selection(view), found_iter);

  return FALSE;
}

static void _action_selection_changed(GtkTreeSelection *selection, gpointer data)
{
  GtkTreeIter iter;
  GtkTreeModel *model = NULL;
  if(!gtk_tree_selection_get_selected(selection, &model, &iter))
    _selected_action = NULL;
  else
  {
    gtk_tree_model_get(model, &iter, 0, &_selected_action, -1);

    GtkTreeView *view = gtk_tree_selection_get_tree_view(selection);
    GtkTreePath *path = gtk_tree_model_get_path(model, &iter);
    gtk_tree_view_expand_row(view, path, FALSE);
    gtk_tree_path_free(path);
  }

  GtkTreeView *shortcuts_view = GTK_TREE_VIEW(data);
  gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(shortcuts_view)));
  gtk_tree_view_expand_all(shortcuts_view);
}

gboolean _search_func(GtkTreeModel *model, gint column, const gchar *key, GtkTreeIter *iter, gpointer search_data)
{
  gboolean different = TRUE;
  if(column == 1)
  {
    dt_action_t *action = NULL;
    gtk_tree_model_get(model, iter, 0, &action, -1);
    different = !strstr(action->label_translated, key);
  }
  else
  {
    GSequenceIter *seq_iter = NULL;
    gtk_tree_model_get(model, iter, 0, &seq_iter, -1);
    if(GPOINTER_TO_UINT(seq_iter) >= NUM_CATEGORIES)
    {
      dt_shortcut_t *s = g_sequence_get(seq_iter);
      if(s->action)
      {
        gchar *label = _action_full_label_translated(s->action);
        different = !strstr(label, key);
        g_free(label);
      }
    }
  }
  if(!different)
  {
    GtkTreePath *path = gtk_tree_model_get_path(model, iter);
    gtk_tree_view_expand_to_path(GTK_TREE_VIEW(search_data), path);
    gtk_tree_path_free(path);

    return FALSE;
  }

  GtkTreeIter child;
  if(gtk_tree_model_iter_children(model, &child, iter))
  {
    do
    {
      _search_func(model, column, key, &child, search_data);
    }
    while(gtk_tree_model_iter_next(model, &child));
  }

  return TRUE;
}

static gboolean _visible_shortcuts(GtkTreeModel *model, GtkTreeIter  *iter, gpointer data)
{
  void *data_ptr = NULL;
  gtk_tree_model_get(model, iter, 0, &data_ptr, -1);

  if(!_selected_action || GPOINTER_TO_UINT(data_ptr) < NUM_CATEGORIES) return TRUE;

  dt_shortcut_t *s = g_sequence_get(data_ptr);
  dt_action_t *ac = s->action;
  while(ac)
  {
    if(ac == _selected_action)
      return TRUE;

    ac = ac->owner;
  }

  return FALSE;
}

static void _resize_shortcuts_view(GtkWidget *view, GdkRectangle *allocation, gpointer data)
{
  dt_conf_set_int("shortcuts/window_split", gtk_paned_get_position(GTK_PANED(data)));
}

GtkWidget *dt_shortcuts_prefs(GtkWidget *widget)
{
  _selected_action = g_hash_table_lookup(darktable.control->widgets, widget);

  GtkWidget *container = gtk_paned_new(GTK_ORIENTATION_VERTICAL);

  // Building the shortcut treeview
  g_set_weak_pointer(&shortcuts_store, gtk_tree_store_new(1, G_TYPE_POINTER)); // static

  const dt_view_t *vw = dt_view_manager_get_current_view(darktable.view_manager);
  dt_view_type_flags_t view = vw && vw->view ? vw->view(vw) : DT_VIEW_LIGHTTABLE;

  for(gint i = 0; i < NUM_CATEGORIES; i++)
    gtk_tree_store_insert_with_values(shortcuts_store, NULL, NULL, -1, 0, GINT_TO_POINTER(i), -1);

  for(GSequenceIter *iter = g_sequence_get_begin_iter(darktable.control->shortcuts);
      !g_sequence_iter_is_end(iter);
      iter = g_sequence_iter_next(iter))
  {
    dt_shortcut_t *s = g_sequence_get(iter);
    GtkTreeIter category;
    shortcuts_store_category(&category, s, view);

    gtk_tree_store_insert_with_values(shortcuts_store, NULL, &category, -1, 0, iter, -1);
  }

  // FIXME fake fallback shortcuts just for illustration
  static GSequence *fakes = NULL;
  static dt_action_t dummy = { .type = DT_ACTION_TYPE_SLIDER, .label_translated = "slider" };
  static dt_shortcut_t s_fine = { .action = &dummy, .mods = GDK_CONTROL_MASK, .effect = -1, .speed = .1 };
  static dt_shortcut_t s_coarse = { .action = &dummy, .mods = GDK_SHIFT_MASK, .effect = -1, .speed = 10. };
  static dt_shortcut_t s_reset = { .action = &dummy, .flags = DT_SHORTCUT_FLAG_BUTTON_LEFT | DT_SHORTCUT_FLAG_CLICK_DOUBLE, .element = 1 };
  if(!fakes)
  {
    fakes = g_sequence_new(NULL);
    g_sequence_append(fakes, &s_coarse);
    g_sequence_append(fakes, &s_fine);
    g_sequence_append(fakes, &s_reset);
  }
  GtkTreeIter category;
  shortcuts_store_category(&category, NULL, 0);
  for(GSequenceIter *i = g_sequence_get_begin_iter(fakes); !g_sequence_iter_is_end(i); i = g_sequence_iter_next(i))
    gtk_tree_store_insert_with_values(shortcuts_store, NULL, &category, -1, 0, i, -1);
  // FIXME end fake fallbacks

  GtkTreeModel *filtered_shortcuts = gtk_tree_model_filter_new(GTK_TREE_MODEL(shortcuts_store), NULL);
  g_object_unref(G_OBJECT(shortcuts_store));

  gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filtered_shortcuts), _visible_shortcuts, NULL, NULL);

  GtkTreeView *shortcuts_view = GTK_TREE_VIEW(gtk_tree_view_new_with_model(filtered_shortcuts));
  g_object_unref(G_OBJECT(filtered_shortcuts));
  gtk_tree_view_set_search_column(shortcuts_view, 0); // fake column for _search_func
  gtk_tree_view_set_search_equal_func(shortcuts_view, _search_func, shortcuts_view, NULL);
  gtk_tree_selection_set_select_function(gtk_tree_view_get_selection(shortcuts_view),
                                         shortcut_selection_function, NULL, NULL);
  g_signal_connect(G_OBJECT(shortcuts_view), "row-activated", G_CALLBACK(_shortcut_row_activated), filtered_shortcuts);
  g_signal_connect(G_OBJECT(shortcuts_view), "key-press-event", G_CALLBACK(_shortcut_key_pressed), NULL);
  g_signal_connect(G_OBJECT(shortcuts_store), "row-inserted", G_CALLBACK(_shortcut_row_inserted), shortcuts_view);

  // Setting up the cell renderers
  _add_prefs_column(shortcuts_view, gtk_cell_renderer_text_new(), _("shortcut"), SHORTCUT_VIEW_DESCRIPTION);

  _add_prefs_column(shortcuts_view, gtk_cell_renderer_text_new(), _("action"), SHORTCUT_VIEW_ACTION);

  GtkCellRenderer *renderer = NULL;

  renderer = gtk_cell_renderer_combo_new();
  GtkListStore *elements = gtk_list_store_new(1, G_TYPE_STRING);
  g_object_set(renderer, "model", elements, "text-column", 0, "has-entry", FALSE, NULL);
  g_signal_connect(renderer, "editing-started" , G_CALLBACK(_element_editing_started), filtered_shortcuts);
  g_signal_connect(renderer, "changed", G_CALLBACK(_element_changed), filtered_shortcuts);
  _add_prefs_column(shortcuts_view, renderer, _("element"), SHORTCUT_VIEW_ELEMENT);

  renderer = gtk_cell_renderer_combo_new();
  GtkListStore *effects = gtk_list_store_new(1, G_TYPE_STRING);
  g_object_set(renderer, "model", effects, "text-column", 0, "has-entry", FALSE, NULL);
  g_signal_connect(renderer, "editing-started" , G_CALLBACK(_effect_editing_started), filtered_shortcuts);
  g_signal_connect(renderer, "changed", G_CALLBACK(_effect_changed), filtered_shortcuts);
  _add_prefs_column(shortcuts_view, renderer, _("effect"), SHORTCUT_VIEW_EFFECT);

  renderer = gtk_cell_renderer_spin_new();
  g_object_set(renderer, "adjustment", gtk_adjustment_new(1, -1000, 1000, .01, 1, 10),
                         "digits", 3, "xalign", 1.0, NULL);
  g_signal_connect(renderer, "edited", G_CALLBACK(_speed_edited), filtered_shortcuts);
  _add_prefs_column(shortcuts_view, renderer, _("speed"), SHORTCUT_VIEW_SPEED);

  renderer = gtk_cell_renderer_combo_new();
  GtkListStore *instances = gtk_list_store_new(1, G_TYPE_STRING);
  for(int i = 0; i < NUM_INSTANCES; i++)
    gtk_list_store_insert_with_values(instances, NULL, -1, 0, _(instance_label[i]), -1);
  for(char relative[] = "-2"; (relative[0] ^= '+' ^ '-') == '-' || ++relative[1] <= '9'; )
    gtk_list_store_insert_with_values(instances, NULL, -1, 0, relative, -1);
  g_object_set(renderer, "model", instances, "text-column", 0, "has-entry", FALSE, NULL);
  g_signal_connect(renderer, "edited", G_CALLBACK(_instance_edited), filtered_shortcuts);
  _add_prefs_column(shortcuts_view, renderer, _("instance"), SHORTCUT_VIEW_INSTANCE);

  // Adding the shortcuts treeview to its containers
  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_widget_set_size_request(scroll, -1, 100);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(shortcuts_view));
  gtk_paned_pack1(GTK_PANED(container), scroll, TRUE, FALSE);

  // Creating the action selection treeview
  g_set_weak_pointer(&actions_store, gtk_tree_store_new(1, G_TYPE_POINTER)); // static
  GtkTreeIter found_iter = {};
  if(widget && !_selected_action)
  {
    const dt_view_t *active_view = dt_view_manager_get_current_view(darktable.view_manager);
    if(gtk_widget_is_ancestor(widget, dt_ui_center_base(darktable.gui->ui)) ||
       dt_ui_panel_ancestor(darktable.gui->ui, DT_UI_PANEL_CENTER_TOP, widget) ||
       dt_ui_panel_ancestor(darktable.gui->ui, DT_UI_PANEL_CENTER_BOTTOM, widget) ||
       gtk_widget_is_ancestor(widget, GTK_WIDGET(dt_ui_get_container(darktable.gui->ui,
                                               DT_UI_CONTAINER_PANEL_LEFT_TOP))) ||
       gtk_widget_is_ancestor(widget, GTK_WIDGET(dt_ui_get_container(darktable.gui->ui,
                                               DT_UI_CONTAINER_PANEL_RIGHT_TOP))))
      _selected_action = (dt_action_t*)active_view;
    else if(dt_ui_panel_ancestor(darktable.gui->ui, DT_UI_PANEL_BOTTOM, widget))
      _selected_action = &darktable.control->actions_thumb;
    else if(dt_ui_panel_ancestor(darktable.gui->ui, DT_UI_PANEL_RIGHT, widget))
      _selected_action = active_view->view(active_view) == DT_VIEW_DARKROOM
                       ? &darktable.control->actions_iops
                       : &darktable.control->actions_libs;
    else if(dt_ui_panel_ancestor(darktable.gui->ui, DT_UI_PANEL_LEFT, widget))
      _selected_action = &darktable.control->actions_libs;
    else
      _selected_action = &darktable.control->actions_global;
  }
  _add_actions_to_tree(NULL, darktable.control->actions, _selected_action, &found_iter);

  GtkTreeView *actions_view = GTK_TREE_VIEW(gtk_tree_view_new_with_model(GTK_TREE_MODEL(actions_store)));
  g_object_unref(actions_store);
  gtk_tree_view_set_search_column(actions_view, 1); // fake column for _search_func
  gtk_tree_view_set_search_equal_func(actions_view, _search_func, actions_view, NULL);
  g_object_set(actions_view, "has-tooltip", TRUE, NULL);
  g_signal_connect(G_OBJECT(actions_view), "query-tooltip", G_CALLBACK(_shortcut_tooltip_callback), actions_store);
  g_signal_connect(G_OBJECT(actions_view), "row-activated", G_CALLBACK(_action_row_activated), actions_store);
  g_signal_connect(G_OBJECT(actions_view), "button-press-event", G_CALLBACK(_action_view_click), actions_store);
  g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(actions_view)), "changed",
                   G_CALLBACK(_action_selection_changed), shortcuts_view);

  renderer = gtk_cell_renderer_text_new();
  GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(_("action"), renderer, NULL);
  gtk_tree_view_column_set_cell_data_func(column, renderer, _show_action_label, NULL, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(actions_view), column);

  // Adding the action treeview to its containers
  scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_widget_set_size_request(scroll, -1, 100);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(actions_view));
  gtk_paned_pack2(GTK_PANED(container), scroll, TRUE, FALSE);

  if(found_iter.user_data)
  {
    GtkTreeIter *send_iter = calloc(1, sizeof(GtkTreeIter));
    *send_iter = found_iter;
    gtk_widget_add_events(GTK_WIDGET(actions_view), GDK_STRUCTURE_MASK);
    g_signal_connect_data(G_OBJECT(actions_view), "map-event", G_CALLBACK(_action_view_map),
                          send_iter, (GClosureNotify)g_free, G_CONNECT_AFTER);
  }

  int split_position = dt_conf_get_int("shortcuts/window_split");
  if(split_position) gtk_paned_set_position(GTK_PANED(container), split_position);
  g_signal_connect(G_OBJECT(shortcuts_view), "size-allocate", G_CALLBACK(_resize_shortcuts_view), container);

  return container;
}

void dt_shortcuts_save(gboolean backup)
{
  char shortcuts_file[PATH_MAX] = { 0 };
  dt_loc_get_user_config_dir(shortcuts_file, sizeof(shortcuts_file));
  g_strlcat(shortcuts_file, "/shortcutsrc", PATH_MAX);
  if(backup)
  {
    gchar *backup_file = g_strdup_printf("%s.backup", shortcuts_file);
    g_rename(shortcuts_file, backup_file);
    g_free(backup_file);
  }
  FILE *f = g_fopen(shortcuts_file, "wb");
  if(f)
  {
    for(GSequenceIter *i = g_sequence_get_begin_iter(darktable.control->shortcuts);
        !g_sequence_iter_is_end(i);
        i = g_sequence_iter_next(i))
    {
      dt_shortcut_t *s = g_sequence_get(i);

      gchar *key_name = _shortcut_key_move_name(s->key_device, s->key, s->mods, FALSE);
      fprintf(f, "%s", key_name);
      g_free(key_name);

      if(s->move_device || s->move)
      {
        gchar *move_name = _shortcut_key_move_name(s->move_device, s->move, DT_MOVE_NAME, FALSE);
        fprintf(f, ";%s", move_name);
        g_free(move_name);
        if(s->flags & DT_SHORTCUT_FLAG_DIR_MASK)
          fprintf(f, ";%s", s->flags & DT_SHORTCUT_FLAG_DIR_UP ? "up" : "down");
      }

      if(s->flags & DT_SHORTCUT_FLAG_PRESS_DOUBLE ) fprintf(f, ";%s", "double");
      if(s->flags & DT_SHORTCUT_FLAG_PRESS_TRIPLE ) fprintf(f, ";%s", "triple");
      if(s->flags & DT_SHORTCUT_FLAG_PRESS_LONG   ) fprintf(f, ";%s", "long");
      if(s->flags & DT_SHORTCUT_FLAG_BUTTON_LEFT  ) fprintf(f, ";%s", "left");
      if(s->flags & DT_SHORTCUT_FLAG_BUTTON_MIDDLE) fprintf(f, ";%s", "middle");
      if(s->flags & DT_SHORTCUT_FLAG_BUTTON_RIGHT ) fprintf(f, ";%s", "right");
      if(s->flags & DT_SHORTCUT_FLAG_CLICK_DOUBLE ) fprintf(f, ";%s", "double");
      if(s->flags & DT_SHORTCUT_FLAG_CLICK_TRIPLE ) fprintf(f, ";%s", "triple");
      if(s->flags & DT_SHORTCUT_FLAG_CLICK_LONG   ) fprintf(f, ";%s", "long");

      fprintf(f, "=");

      gchar *action_label = _action_full_label(s->action);
      fprintf(f, "%s", action_label);
      g_free(action_label);

      const dt_shortcut_element *elements = _action_find_elements(s->action);
      if(s->element)
        fprintf(f, ";%s", elements[s->element].name);
      if(s->effect > (_shortcut_is_move(s) ? DT_SHORTCUT_EFFECT_DEFAULT_MOVE
                                           : DT_SHORTCUT_EFFECT_DEFAULT_KEY))
        fprintf(f, ";%s", elements[s->element].effects[s->effect]);

      if(s->instance == -1) fprintf(f, ";last");
      if(s->instance == +1) fprintf(f, ";first");
      if(abs(s->instance) > 1) fprintf(f, ";%+d", s->instance);
      if(s->speed != 1.0) fprintf(f, ";*%g", s->speed);

      fprintf(f, "\n");
    }

    fclose(f);
  }
}

void dt_shortcuts_load(gboolean clear)
{
  char shortcuts_file[PATH_MAX] = { 0 };
  dt_loc_get_user_config_dir(shortcuts_file, sizeof(shortcuts_file));
  g_strlcat(shortcuts_file, "/shortcutsrc", PATH_MAX);
  if(!g_file_test(shortcuts_file, G_FILE_TEST_EXISTS))
    return;

  // start with an empty shortcuts collection
  if(clear && darktable.control->shortcuts)
  {
    g_sequence_free(darktable.control->shortcuts);
    darktable.control->shortcuts = g_sequence_new(g_free);
  }

  FILE *f = g_fopen(shortcuts_file, "rb");
  if(f)
  {
    while(!feof(f))
    {
      char line[1024];
      char *read = fgets(line, sizeof(line), f);
      if(read > 0)
      {
        line[strcspn(line, "\r\n")] = '\0';

        char *act_start = strchr(line, '=');
        if(!act_start)
        {
          fprintf(stderr, "[dt_shortcuts_load] line '%s' is not an assignment\n", line);
          continue;
        }

        dt_shortcut_t s = { .speed = 1 };

        char *token = strtok(line, "=;");
        if(strcmp(token, "None"))
        {
          char *colon = strchr(token, ':');
          if(!colon)
          {
            gtk_accelerator_parse(token, &s.key, &s.mods);
            if(s.mods) fprintf(stderr, "[dt_shortcuts_load] unexpected modifiers found in %s\n", token);
            if(!s.key) fprintf(stderr, "[dt_shortcuts_load] no key name found in %s\n", token);
          }
          else
          {
            char *key_start = colon + 1;
            *colon-- = 0;
            if(colon == token)
            {
              fprintf(stderr, "[dt_shortcuts_load] missing driver name in %s\n", token);
              continue;
            }
            dt_input_device_t id = *colon - '0';
            if(id > 9 )
              id = 0;
            else
              *colon-- = 0;

            GSList *driver = darktable.control->input_drivers;
            while(driver)
            {
              id += 10;
              dt_input_driver_definition_t *callbacks = driver->data;
              if(!strcmp(token, callbacks->name))
              {
                if(!callbacks->string_to_key(key_start, &s.key))
                  fprintf(stderr, "[dt_shortcuts_load] key not recognised in %s\n", key_start);

                s.key_device = id;
                break;
              }
              driver = driver->next;
            }
            if(!driver)
            {
              fprintf(stderr, "[dt_shortcuts_load] '%s' is not a valid driver\n", token);
              continue;
            }
          }
        }

        while((token = strtok(NULL, "=;")) && token < act_start)
        {
          char *colon = strchr(token, ':');
          if(!colon)
          {
            int mod = -1;
            while(modifier_string[++mod].modifier)
              if(!strcmp(token, modifier_string[mod].name)) break;
            if(modifier_string[mod].modifier)
            {
              s.mods |= modifier_string[mod].modifier;
              continue;
            }

            if(!strcmp(token, "left"  )) { s.flags |= DT_SHORTCUT_FLAG_BUTTON_LEFT  ; continue; }
            if(!strcmp(token, "middle")) { s.flags |= DT_SHORTCUT_FLAG_BUTTON_MIDDLE; continue; }
            if(!strcmp(token, "right" )) { s.flags |= DT_SHORTCUT_FLAG_BUTTON_RIGHT ; continue; }

            if(s.flags & DT_SHORTCUT_FLAG_BUTTON_MASK)
            {
              if(!strcmp(token, "double")) { s.flags |= DT_SHORTCUT_FLAG_CLICK_DOUBLE; continue; }
              if(!strcmp(token, "triple")) { s.flags |= DT_SHORTCUT_FLAG_CLICK_TRIPLE; continue; }
              if(!strcmp(token, "long"  )) { s.flags |= DT_SHORTCUT_FLAG_CLICK_LONG  ; continue; }
            }
            else
            {
              if(!strcmp(token, "double")) { s.flags |= DT_SHORTCUT_FLAG_PRESS_DOUBLE; continue; }
              if(!strcmp(token, "triple")) { s.flags |= DT_SHORTCUT_FLAG_PRESS_TRIPLE; continue; }
              if(!strcmp(token, "long"  )) { s.flags |= DT_SHORTCUT_FLAG_PRESS_LONG  ; continue; }
            }

            int move = 0;
            while(move_string[++move])
              if(!strcmp(token, move_string[move])) break;
            if(move_string[move])
            {
              s.move = move;
              continue;
            }

            if(!strcmp(token, "up"  )) { s.flags |= DT_SHORTCUT_FLAG_DIR_UP  ; continue; }
            if(!strcmp(token, "down")) { s.flags |= DT_SHORTCUT_FLAG_DIR_DOWN; continue; }

            fprintf(stderr, "[dt_shortcuts_load] token '%s' not recognised\n", token);
          }
          else
          {
            char *move_start = colon + 1;
            *colon-- = 0;
            if(colon == token)
            {
              fprintf(stderr, "[dt_shortcuts_load] missing driver name in %s\n", token);
              continue;
            }
            dt_input_device_t id = *colon - '0';
            if(id > 9 )
              id = 0;
            else
              *colon-- = 0;

            GSList *driver = darktable.control->input_drivers;
            while(driver)
            {
              id += 10;
              dt_input_driver_definition_t *callbacks = driver->data;
              if(!strcmp(token, callbacks->name))
              {
                if(!callbacks->string_to_move(move_start, &s.move))
                  fprintf(stderr, "[dt_shortcuts_load] move not recognised in %s\n", move_start);

                s.move_device = id;
                break;
              }
              driver = driver->next;
            }
            if(!driver)
            {
              fprintf(stderr, "[dt_shortcuts_load] '%s' is not a valid driver\n", token);
              continue;
            }
          }
        }

        // find action and also views along the way
        gchar **path = g_strsplit(token, "/", 0);
        s.action = dt_action_locate(NULL, path);
        g_strfreev(path);

        if(!s.action)
        {
          fprintf(stderr, "[dt_shortcuts_load] action path '%s' not found\n", token);
          continue;
        }

        const dt_shortcut_element *elements = _action_find_elements(s.action);

        gint default_effect = s.effect = _shortcut_is_move(&s)
                                       ? DT_SHORTCUT_EFFECT_DEFAULT_MOVE
                                       : DT_SHORTCUT_EFFECT_DEFAULT_KEY;

        while((token = strtok(NULL, ";")))
        {
          if(elements)
          {
            int element = 0;
            while(elements[++element].name)
              if(!strcmp(token, elements[element].name)) break;
            if(elements[element].name)
            {
              s.element = element;
              s.effect = default_effect; // reset if an effect for a different element was found first
              continue;
            }

            const gchar **effects = elements[s.element].effects;
            int effect = -1;
            while(effects[++effect])
              if(!strcmp(token, effects[effect])) break;
            if(effects[effect])
            {
              s.effect = effect;
              continue;
            }
          }

          if(!strcmp(token, "first")) s.instance =  1; else
          if(!strcmp(token, "last" )) s.instance = -1; else
          if(*token == '+' || *token == '-') sscanf(token, "%d", &s.instance); else
          if(*token == '*') sscanf(token, "*%g", &s.speed); else
          fprintf(stderr, "[dt_shortcuts_load] token '%s' not recognised\n", token);
        }

        insert_shortcut(&s, FALSE);
      }
    }
    fclose(f);
  }
}

void dt_shortcuts_reinitialise()
{
  for(GSList *d = darktable.control->input_drivers; d; d = d->next)
  {
    dt_input_driver_definition_t *driver = d->data;
    driver->module->gui_cleanup(driver->module);
    driver->module->gui_init(driver->module);
  }

  // reload shortcuts
  dt_shortcuts_load(TRUE);

  char actions_file[PATH_MAX] = { 0 };
  dt_loc_get_user_config_dir(actions_file, sizeof(actions_file));
  g_strlcat(actions_file, "/all_actions", PATH_MAX);
  FILE *f = g_fopen(actions_file, "wb");
  _dump_actions(f, darktable.control->actions);
  fclose(f);

  dt_control_log(_("input devices reinitialised"));
}

void dt_shortcuts_select_view(dt_view_type_flags_t view)
{
  g_sequence_sort(darktable.control->shortcuts, shortcut_compare_func, GINT_TO_POINTER(view));
}

static GSList *pressed_keys = NULL; // list of currently pressed keys
static guint _pressed_button = 0;
static guint _last_time = 0;
static guint _timeout_source = 0;

static void lookup_mapping_widget()
{
  _sc.action = g_hash_table_lookup(darktable.control->widgets, darktable.control->mapping_widget);
  if(_sc.action->target != darktable.control->mapping_widget)
  {
    // find relative module instance
    dt_action_t *owner = _sc.action->owner;
    while(owner && owner->type != DT_ACTION_TYPE_IOP) owner = owner->owner;
    if(owner)
    {
      dt_iop_module_so_t *module = (dt_iop_module_so_t *)owner;

      int current_instance = 0;
      for(GList *iop_mods = darktable.develop->iop;
          iop_mods;
          iop_mods = g_list_next(iop_mods))
      {
        dt_iop_module_t *mod = (dt_iop_module_t *)iop_mods->data;

        if(mod->so == module && mod->iop_order != INT_MAX)
        {
          current_instance++;

          if(!_sc.instance)
          {
            for(GSList *w = mod->widget_list; w; w = w->next)
            {
              if(((dt_action_target_t *)w->data)->target == darktable.control->mapping_widget)
              {
                _sc.instance = current_instance;
                break;
              }
            }
          }
        }
      }

      if(current_instance - _sc.instance < _sc.instance) _sc.instance -= current_instance + 1;
    }
  }
}

static void define_new_mapping()
{
  if(insert_shortcut(&_sc, TRUE))
  {
    gchar *label = _action_full_label_translated(_sc.action);
    dt_control_log(_("%s assigned to %s"), _shortcut_description(&_sc, TRUE), label);
    g_free(label);
  }

  _sc.instance = 0;
  darktable.control->mapping_widget = _sc.action = NULL;

  dt_shortcuts_save(FALSE);
}

static gboolean _widget_invisible(GtkWidget *w)
{
  return (!GTK_IS_WIDGET(w) ||
          !gtk_widget_get_visible(w) ||
          !gtk_widget_get_visible(gtk_widget_get_parent(w)));
}

gboolean combobox_idle_value_changed(gpointer widget)
{
  g_signal_emit_by_name(G_OBJECT(widget), "value-changed");

  while(g_idle_remove_by_data(widget));

  return FALSE;
}

static float process_mapping(float move_size)
{
  float return_value = NAN;

  _sc.views = darktable.view_manager->current_view->view(darktable.view_manager->current_view);

  GSequenceIter *existing = g_sequence_lookup(darktable.control->shortcuts, &_sc,
                                              shortcut_compare_func, GINT_TO_POINTER(_sc.views));
  if(existing)
  {
    dt_shortcut_t *bac = g_sequence_get(existing);

    dt_action_t *owner = bac->action;
    while(owner && owner->type >= DT_ACTION_TYPE_SECTION) owner = owner->owner;

    GtkWidget *widget = bac->action->target;

    dt_iop_module_t *mod = NULL;
    if(owner && owner->type == DT_ACTION_TYPE_IOP &&
       (bac->instance ||
        bac->action->type == DT_ACTION_TYPE_IOP ||
        bac->action->type == DT_ACTION_TYPE_PRESET))
    {
      // find module instance
      dt_iop_module_so_t *module = (dt_iop_module_so_t *)owner;

      int current_instance = abs(bac->instance);

      for(GList *iop_mods = bac->instance >= 0
                          ? darktable.develop->iop
                          : g_list_last(darktable.develop->iop);
          iop_mods;
          iop_mods = bac->instance >= 0
                   ? g_list_next(iop_mods)
                   : g_list_previous(iop_mods))
      {
        mod = (dt_iop_module_t *)iop_mods->data;

        if(mod->widget_list)
        {
          dt_action_target_t *referral = mod->widget_list->data;
          // if instance == 0 then check this is currently selected preferred
          if(!bac->instance && referral->target == referral->action->target)
            break;
        }

        if(mod->so == module &&
           mod->iop_order != INT_MAX &&
           (!--current_instance))
          break;
      }

      // find module instance widget
      if(mod && bac->action->type >= DT_ACTION_TYPE_PER_INSTANCE)
      {
        for(GSList *w = mod->widget_list; w; w = w->next)
        {
          dt_action_target_t *referral = w->data;
          if(referral->action == bac->action)
          {
            widget = referral->target;
            break;
          }
        }
      }
    }

    if(bac->action->type == DT_ACTION_TYPE_PRESET && owner && move_size != 0)
    {
      if(owner->type == DT_ACTION_TYPE_LIB)
      {
        dt_lib_module_t *lib = (dt_lib_module_t *)owner;
        dt_lib_presets_apply(bac->action->label_translated, lib->plugin_name, lib->version());
      }
      else if(owner->type == DT_ACTION_TYPE_IOP)
      {
        dt_gui_presets_apply_preset(bac->action->label_translated, mod);
      }
    }
    else if(bac->action->type == DT_ACTION_TYPE_WIDGET && !_widget_invisible(widget) && move_size != 0)
    {
      if(GTK_IS_BUTTON(widget))
      {
        GdkEvent *event = gdk_event_new(GDK_BUTTON_PRESS);
        event->button.state = 0; // FIXME support ctrl-press
        event->button.button = GDK_BUTTON_PRIMARY;
        GdkWindow *w = gtk_widget_get_window(widget);
        if(w)
        {
          event->button.window = w;
          g_object_ref(event->button.window);

          // some togglebuttons connect to the clicked signal, others to toggled or button-press-event
          if(!gtk_widget_event(widget, event))
            gtk_button_clicked(GTK_BUTTON(widget));
        }

        gdk_event_free(event);
      }
      else
        return return_value;
    }
    else if(bac->action->type == DT_ACTION_TYPE_SLIDER && !_widget_invisible(widget))
    {
      dt_bauhaus_widget_t *bhw = DT_BAUHAUS_WIDGET(widget);
      dt_bauhaus_slider_data_t *d = &bhw->data.slider;

      if(move_size != 0)
      {
        switch(bac->effect)
        {
        case DT_SHORTCUT_EFFECT_RESET:
          dt_bauhaus_slider_reset(widget);
          break;
        case DT_SHORTCUT_EFFECT_TOP:
          dt_bauhaus_slider_set(widget, d->max);
          break;
        case DT_SHORTCUT_EFFECT_BOTTOM:
          dt_bauhaus_slider_set(widget, d->min);
          break;
        case DT_SHORTCUT_EFFECT_DOWN:
          move_size *= -1;
        case DT_SHORTCUT_EFFECT_UP:
        case DT_SHORTCUT_EFFECT_DEFAULT_MOVE:
          move_size *= bac->speed;
          float value = dt_bauhaus_slider_get(widget);
          float step = dt_bauhaus_slider_get_step(widget);
          float multiplier = dt_accel_get_slider_scale_multiplier();

          const float min_visible = powf(10.0f, -dt_bauhaus_slider_get_digits(widget));
          if(fabsf(step*multiplier) < min_visible)
            multiplier = min_visible / fabsf(step);

          d->is_dragging = 1;
          dt_bauhaus_slider_set(widget, value + move_size * step * multiplier);
          d->is_dragging = 0;
          break;
        case DT_SHORTCUT_EFFECT_EDIT:
          dt_bauhaus_show_popup(DT_BAUHAUS_WIDGET(widget));
          break;
        default:
          fprintf(stderr, "[process_mapping] unknown shortcut effect (%d) for slider\n", bac->effect);
          break;
        }

        dt_accel_widget_toast(widget);
      }

      return_value = d->pos +
                  ( d->min == -d->max ? 2 :
                  ( d->min == 0 && (d->max == 1 || d->max == 100) ? 4 : 0 ));
    }
    else if(bac->action->type == DT_ACTION_TYPE_COMBO && !_widget_invisible(widget))
    {
      int value = dt_bauhaus_combobox_get(widget);

      if(move_size != 0)
      {
        switch(bac->effect)
        {
        case DT_SHORTCUT_EFFECT_RESET:
          dt_bauhaus_slider_reset(widget);
          break;
        case DT_SHORTCUT_EFFECT_FIRST:
          move_size *= -1; // negate again below at DT_SHORTCUT_EFFECT_NEXT
        case DT_SHORTCUT_EFFECT_LAST:
          move_size *= 1e6;
        case DT_SHORTCUT_EFFECT_NEXT:
          move_size *= -1;
        case DT_SHORTCUT_EFFECT_PREVIOUS:
        case DT_SHORTCUT_EFFECT_DEFAULT_MOVE:
          value = CLAMP(value + move_size, 0, dt_bauhaus_combobox_length(widget) - 1);

          ++darktable.gui->reset;
          dt_bauhaus_combobox_set(widget, value);
          --darktable.gui->reset;

          g_idle_add(combobox_idle_value_changed, widget);

          dt_accel_widget_toast(widget);
          break;
        case DT_SHORTCUT_EFFECT_EDIT:
          dt_bauhaus_show_popup(DT_BAUHAUS_WIDGET(widget));
          break;
        default:
          fprintf(stderr, "[process_mapping] unknown shortcut effect (%d) for combo\n", bac->effect);
          break;
        }
      }

      return_value = - 1 - value;
    }
    else if(bac->action->type == DT_ACTION_TYPE_IOP && move_size != 0)
    {

    }
    else if(bac->action->type == DT_ACTION_TYPE_CLOSURE && bac->action->target && move_size != 0)
    {
      typedef gboolean (*accel_callback)(GtkAccelGroup *accel_group, GObject *acceleratable,
                                        guint keyval, GdkModifierType modifier, gpointer p);
      ((accel_callback)((GCClosure*)widget)->callback)(NULL, NULL, bac->key, bac->mods,
                                                       ((GClosure*)widget)->data);
    }
  }

  return return_value;
}

gint cmp_key(gconstpointer a, gconstpointer b)
{
  const dt_device_key_t *key_a = a;
  const dt_device_key_t *key_b = b;
  return key_a->key_device != key_b->key_device || key_a->key != key_b->key;
}

float dt_shortcut_move(dt_input_device_t id, guint time, guint move, double size)
{
  if(grab_widget)
    ungrab_grab_widget();

  _sc.move_device = id;
  _sc.move = move;
  _sc.speed = 1.0;
  _sc.effect = _shortcut_is_move(&_sc) ? DT_SHORTCUT_EFFECT_DEFAULT_MOVE : DT_SHORTCUT_EFFECT_DEFAULT_KEY;
  _sc.flags |= size > 0 ? DT_SHORTCUT_FLAG_DIR_UP
             : size < 0 ? DT_SHORTCUT_FLAG_DIR_DOWN : 0;

  float return_value = 0;
  GdkKeymap *keymap = gdk_keymap_get_for_display(gdk_display_get_default());

  if(!pressed_keys && !_sc.key_device && !_sc.key)
    _sc.mods = gdk_keymap_get_modifier_state(keymap);

  _sc.mods &= gdk_keymap_get_modifier_mask(keymap, GDK_MODIFIER_INTENT_DEFAULT_MOD_MASK);
  gdk_keymap_add_virtual_modifiers(keymap, &_sc.mods);

  if(darktable.control->mapping_widget && !_sc.action && size != 0) lookup_mapping_widget();

   dt_print(DT_DEBUG_INPUT, "  [dt_shortcut_move] shortcut received: %s\n", _shortcut_description(&_sc, TRUE));

   if(_sc.action)
  {
    define_new_mapping();
  }
  else
  {
    if(pressed_keys)
    {
      for(GSList *k = pressed_keys; k; k = k->next)
      {
        dt_device_key_t *device_key = k->data;
        _sc.key_device = device_key->key_device;
        _sc.key = device_key->key;

        return_value = process_mapping(size);
      }
    }
    else
      return_value = process_mapping(size);
  }

  _sc.move_device = 0;
  _sc.move = DT_SHORTCUT_MOVE_NONE;
  _sc.flags &= ~DT_SHORTCUT_FLAG_DIR_MASK;

  return return_value;
}

static gboolean _key_up_delayed(gpointer timed_out)
{
  if(!pressed_keys) ungrab_grab_widget();

  if(!timed_out)
    dt_shortcut_move(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, 0, DT_SHORTCUT_MOVE_NONE, 1);

  _sc.key_device = DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE;
  _sc.key = 0;
  _sc.flags = 0;
  _sc.mods = 0;

  _timeout_source = 0;
  _last_time = 0;

  return FALSE;
}

static gboolean _button_release_delayed(gpointer timed_out)
{
  if(!timed_out)
    dt_shortcut_move(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, 0, DT_SHORTCUT_MOVE_NONE, 1);

  _sc.flags = (_sc.flags & DT_SHORTCUT_FLAG_PRESS_MASK) | _pressed_button;

  _timeout_source = 0;
  _last_time = 0;

  return FALSE;
}

void dt_shortcut_key_press(dt_input_device_t id, guint time, guint key, guint mods)
{
  dt_device_key_t this_key = { id, key };
  if(!g_slist_find_custom(pressed_keys, &this_key, cmp_key))
  {
    if(_timeout_source)
    {
      g_source_remove(_timeout_source);
      _timeout_source = 0;
    }

    int delay = 0;
    g_object_get(gtk_settings_get_default(), "gtk-double-click-time", &delay, NULL);

    if(!pressed_keys)
    {
      _sc.mods = mods;
      if(id == _sc.key_device && key == _sc.key && time < _last_time + delay &&
         !(_sc.flags & DT_SHORTCUT_FLAG_PRESS_TRIPLE))
        _sc.flags += DT_SHORTCUT_FLAG_PRESS_DOUBLE;
      else
      {
        _sc.flags = 0;

        if(darktable.control->mapping_widget && !_sc.action) lookup_mapping_widget();
      }

      GdkCursor *cursor = gdk_cursor_new_from_name(gdk_display_get_default(), "all-scroll");
      gdk_seat_grab(gdk_display_get_default_seat(gdk_display_get_default()),
                    gtk_widget_get_window(grab_window ? grab_window
                                                      : dt_ui_main_window(darktable.gui->ui)),
                    GDK_SEAT_CAPABILITY_ALL, FALSE, cursor,
                    NULL, NULL, NULL);
      g_object_unref(cursor);
    }

    _last_time = time;
    _sc.key_device = id;
    _sc.key = key;
    _sc.flags &= DT_SHORTCUT_FLAG_PRESS_MASK;
    _pressed_button = 0;

    dt_device_key_t *new_key = calloc(1, sizeof(dt_device_key_t));
    *new_key = this_key;
    pressed_keys = g_slist_prepend(pressed_keys, new_key);
  }
}

void dt_shortcut_key_release(dt_input_device_t id, guint time, guint key)
{
  dt_device_key_t this_key = { id, key };

  GSList *stored_key = g_slist_find_custom(pressed_keys, &this_key, cmp_key);
  if(stored_key)
  {
    g_free(stored_key->data);
    pressed_keys = g_slist_delete_link(pressed_keys, stored_key);

    if(!pressed_keys)
    {
      if(_sc.key_device == id && _sc.key == key)
      {
        int delay = 0;
        g_object_get(gtk_settings_get_default(), "gtk-double-click-time", &delay, NULL);

        guint passed_time = time - _last_time;
        if(passed_time < delay && !(_sc.flags & DT_SHORTCUT_FLAG_PRESS_TRIPLE))
        {
          if(!_timeout_source)
            _timeout_source = g_timeout_add(delay - passed_time, _key_up_delayed, NULL);
        }
        else
        {
          if(passed_time > delay) _sc.flags |= DT_SHORTCUT_FLAG_PRESS_LONG;
          _key_up_delayed(GINT_TO_POINTER(passed_time > 2 * delay)); // call immediately
        }
      }
      else
      {
        _key_up_delayed(GINT_TO_POINTER(TRUE)); // not sequence of same key
      }
    }
  }
  else
  {
    fprintf(stderr, "[dt_shortcut_key_release] released key wasn't stored\n");
  }
}

static guint _fix_keyval(GdkEvent *event)
{
  guint keyval = 0;
  GdkKeymap *keymap = gdk_keymap_get_for_display(gdk_display_get_default());
  gdk_keymap_translate_keyboard_state(keymap, event->key.hardware_keycode, 0, 0,
                                      &keyval, NULL, NULL, NULL);
  return keyval;
}

gboolean dt_shortcut_dispatcher(GtkWidget *w, GdkEvent *event, gpointer user_data)
{
  static gdouble move_start_x = 0;
  static gdouble move_start_y = 0;

//  dt_print(DT_DEBUG_INPUT, "  [shortcut_dispatcher] %d\n", event->type);

  if(!darktable.control->key_accelerators_on) return FALSE; // FIXME should eventually no longer be needed

  if(pressed_keys == NULL)
  {
    if(grab_widget && event->type == GDK_BUTTON_PRESS)
    {
      ungrab_grab_widget();
      return TRUE;
    }

    if(event->type != GDK_KEY_PRESS && event->type != GDK_FOCUS_CHANGE)
      return FALSE;

    if(GTK_IS_WINDOW(w))
    {
      GtkWidget *focused = gtk_window_get_focus(GTK_WINDOW(w));
      if(focused && gtk_widget_event(focused, event))
        return TRUE;
    }
  }

  switch(event->type)
  {
  case GDK_KEY_PRESS:
    if(event->key.is_modifier) return FALSE;

    // FIXME: eventually clean up per-view and global key_pressed handlers
    if(dt_control_key_pressed_override(event->key.keyval, dt_gui_translated_key_state(&event->key))) return TRUE;

    dt_shortcut_key_press(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, event->key.time, _fix_keyval(event), event->key.state);
    break;
  case GDK_KEY_RELEASE:
    if(event->key.is_modifier) return FALSE;

    // FIXME: release also handled by window key_released, in case not in shortcut grab
    if(dt_control_key_pressed_override(event->key.keyval, dt_gui_translated_key_state(&event->key))) return TRUE;

    dt_shortcut_key_release(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, event->key.time, _fix_keyval(event));
    break;
  case GDK_GRAB_BROKEN:
    if(event->grab_broken.implicit) break;
  case GDK_WINDOW_STATE:
    event->focus_change.in = FALSE; // fall through to GDK_FOCUS_CHANGE
  case GDK_FOCUS_CHANGE: // dialog boxes and switch to other app release grab
    if(event->focus_change.in)
      g_set_weak_pointer(&grab_window, w);
    else
    {
      grab_window = NULL;
      ungrab_grab_widget();
      g_slist_free_full(pressed_keys, g_free);
      pressed_keys = NULL;
      _sc.flags = 0;
    }
    break;
  case GDK_SCROLL:
    {
      int delta_x, delta_y;
      if(dt_gui_get_scroll_unit_deltas((GdkEventScroll *)event, &delta_x, &delta_y))
      {
        if(delta_x)
          dt_shortcut_move(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, event->scroll.time, DT_SHORTCUT_MOVE_PAN, -delta_x);
        if(delta_y)
          dt_shortcut_move(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, event->scroll.time, DT_SHORTCUT_MOVE_SCROLL, -delta_y);
      }
    }
    break;
  case GDK_MOTION_NOTIFY:
    if(_sc.move == DT_SHORTCUT_MOVE_NONE)
    {
      move_start_x = event->motion.x;
      move_start_y = event->motion.y;
      _sc.move = DT_SHORTCUT_MOVE_HORIZONTAL; // set fake direction so the start position doesn't keep resetting
      break;
    }

    gdouble x_move = event->motion.x - move_start_x;
    gdouble y_move = event->motion.y - move_start_y;
    const gdouble step_size = 10; // FIXME configurable, x & y separately

//  FIXME try to keep cursor in same location. gdk_device_warp does not seem to do anything. Maybe needs different device?
//  gdk_device_warp(event->motion.device, gdk_window_get_screen(event->motion.window),
//                  move_start_x, move_start_y); // use event->motion.x_root

    gdouble angle = x_move / (0.001 + y_move);

    gdouble size = trunc(x_move / step_size);
    if(size != 0 && fabs(angle) >= 2)
    {
      move_start_x += size * step_size;
      move_start_y = event->motion.y;
      dt_shortcut_move(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, event->motion.time, DT_SHORTCUT_MOVE_HORIZONTAL, size);
    }
    else
    {
      size = - trunc(y_move / step_size);
      if(size != 0)
      {
        move_start_y -= size * step_size;
        if(fabs(angle) < .5)
        {
          move_start_x = event->motion.x;
          dt_shortcut_move(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, event->motion.time, DT_SHORTCUT_MOVE_VERTICAL, size);
        }
        else
        {
          move_start_x -= size * step_size * angle;
          dt_shortcut_move(DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE, event->motion.time,
                           angle < 0 ? DT_SHORTCUT_MOVE_SKEW : DT_SHORTCUT_MOVE_DIAGONAL, size);
        }
      }
    }
    break;
  case GDK_BUTTON_PRESS:
    _pressed_button |= DT_SHORTCUT_FLAG_BUTTON_LEFT << (event->button.button - 1);
    _sc.flags = (_sc.flags & DT_SHORTCUT_FLAG_PRESS_MASK) | _pressed_button;
    _last_time = event->button.time;
    if(_timeout_source)
    {
      g_source_remove(_timeout_source);
      _timeout_source = 0;
    }
    break;
  case GDK_DOUBLE_BUTTON_PRESS:
    _sc.flags |= DT_SHORTCUT_FLAG_CLICK_DOUBLE;
    break;
  case GDK_TRIPLE_BUTTON_PRESS:
    _sc.flags |= DT_SHORTCUT_FLAG_CLICK_TRIPLE;
    break;
  case GDK_BUTTON_RELEASE:
    // FIXME; check if there's a shortcut defined for double/triple (could be fallback?); if not -> no delay
    // maybe even action on PRESS rather than RELEASE
    // FIXME be careful!!; we seem to be receiving presses and releases twice!?!
    _pressed_button &= ~(DT_SHORTCUT_FLAG_BUTTON_LEFT << (event->button.button - 1));

    int delay = 0;
    g_object_get(gtk_settings_get_default(), "gtk-double-click-time", &delay, NULL);

    guint passed_time = event->button.time - _last_time;
    if(passed_time < delay && !(_sc.flags & DT_SHORTCUT_FLAG_CLICK_TRIPLE))
    {
      if(!_timeout_source)
        _timeout_source = g_timeout_add(delay - passed_time, _button_release_delayed, NULL);
    }
    else
    {
      if(passed_time > delay)
        _sc.flags |= DT_SHORTCUT_FLAG_CLICK_LONG;
      _button_release_delayed(GINT_TO_POINTER(passed_time > 2 * delay)); // call immediately
    }
    break;
  default:
    return FALSE;
  }

  return TRUE;
}

static void _remove_widget_from_hashtable(GtkWidget *widget, gpointer user_data)
{
  dt_action_t *action = g_hash_table_lookup(darktable.control->widgets, widget);
  if(action && action->target == widget)
  {
    action->target = NULL;
    g_hash_table_remove(darktable.control->widgets, widget);
  }
}

static inline gchar *path_without_symbols(const gchar *path)
{
  return g_strdelimit(g_strdup(path), "=,/.", '-');
}

void dt_action_insert_sorted(dt_action_t *owner, dt_action_t *new_action)
{
  dt_action_t **insertion_point = (dt_action_t **)&owner->target;
  while(*insertion_point &&
      g_utf8_collate((*insertion_point)->label_translated, new_action->label_translated) < 0)
  {
    insertion_point = &(*insertion_point)->next;
  }
  new_action->next = *insertion_point;
  *insertion_point = new_action;
}

dt_action_t *dt_action_locate(dt_action_t *owner, gchar **path)
{
//  if(!owner) return NULL;

  gchar *clean_path = NULL;

  dt_action_t *action = owner ? owner->target : darktable.control->actions;
  while(*path)
  {
    if(!clean_path) clean_path = path_without_symbols(*path);

    if(!action)
    {
      dt_action_t *new_action = calloc(1, sizeof(dt_action_t));
      new_action->label = clean_path;
      new_action->label_translated = g_strdup(Q_(*path));
      new_action->type = DT_ACTION_TYPE_SECTION;
      new_action->owner = owner;

      dt_action_insert_sorted(owner, new_action);

      owner = new_action;
      action = NULL;
    }
    else if(!strcmp(action->label, clean_path))
    {
      g_free(clean_path);
      owner = action;
      action = action->target;
    }
    else
    {
      action = action->next;
      continue;
    }
    clean_path = NULL; // now owned by action or freed
    path++;
  }

  if(owner->type <= DT_ACTION_TYPE_SECTION && owner->target)
  {
    fprintf(stderr, "[dt_action_locate] found action '%s' not leaf node \n", owner->label);
    return NULL;
  }
  else if(owner->type == DT_ACTION_TYPE_SECTION)
    owner->type = DT_ACTION_TYPE_CLOSURE; // mark newly created leaf as closure

  return owner;
}

void dt_action_define_key_pressed_accel(dt_action_t *action, const gchar *path, GtkAccelKey *key)
{
  dt_action_t *new_action = calloc(1, sizeof(dt_action_t));
  new_action->label = path_without_symbols(path);
  new_action->label_translated = g_strdup(Q_(path));
  new_action->type = DT_ACTION_TYPE_KEY_PRESSED;
  new_action->target = key;
  new_action->owner = action;

  dt_action_insert_sorted(action, new_action);
}

dt_action_t *_action_define(dt_action_t *owner, const gchar *path, gboolean local, guint accel_key, GdkModifierType mods, GtkWidget *widget)
{
  // add to module_so actions list
  // split on `; find any sections or if not found, create (at start)
  gchar **split_path = g_strsplit(path, "`", 6);
  dt_action_t *ac = dt_action_locate(owner, split_path);
  g_strfreev(split_path);

  if(ac)
  {
    if(owner->type == DT_ACTION_TYPE_CLOSURE && owner->target)
      g_closure_unref(owner->target);


    ac->type = DT_IS_BAUHAUS_WIDGET(widget)
             ? DT_BAUHAUS_WIDGET(widget)->type == DT_BAUHAUS_SLIDER
             ? DT_ACTION_TYPE_SLIDER
             : DT_ACTION_TYPE_COMBO
             : DT_ACTION_TYPE_WIDGET;

    if(!darktable.control->accel_initialising)
    {
      ac->target = widget;
      g_hash_table_insert(darktable.control->widgets, widget, ac);

      // in case of bauhaus widget more efficient to directly implement in dt_bauhaus_..._destroy
      g_signal_connect(G_OBJECT(widget), "query-tooltip", G_CALLBACK(_shortcut_tooltip_callback), NULL);
      g_signal_connect(G_OBJECT(widget), "destroy", G_CALLBACK(_remove_widget_from_hashtable), NULL);
    }
  }

  return ac;
}

void dt_action_define_iop(dt_iop_module_t *self, const gchar *path, gboolean local, guint accel_key, GdkModifierType mods, GtkWidget *widget)
{
  // add to module_so actions list
  dt_action_t *ac = strstr(path,"blend`") == path
                  ? _action_define(&darktable.control->actions_blend, path + strlen("blend`"), local, accel_key, mods, widget)
                  : _action_define(&self->so->actions, path, local, accel_key, mods, widget);

  // to support multi-instance, also save in per instance widget list
  dt_action_target_t *referral = g_malloc0(sizeof(dt_action_target_t));
  referral->action = ac;
  referral->target = widget;
  self->widget_list = g_slist_prepend(self->widget_list, referral);
}

void dt_accel_register_shortcut(dt_action_t *owner, const gchar *path_string, guint accel_key, GdkModifierType mods)
{
  gchar **split_path = g_strsplit(path_string, "/", 0);
  gchar **split_trans = g_strsplit(g_dpgettext2(NULL, "accel", path_string), "/", g_strv_length(split_path));

  gchar **path = split_path;
  gchar **trans = split_trans;

  gchar *clean_path = NULL;

  dt_action_t *action = owner->target;
  while(*path)
  {
    if(!clean_path) clean_path = path_without_symbols(*path);

    if(!action)
    {
      dt_action_t *new_action = calloc(1, sizeof(dt_action_t));
      new_action->label = clean_path;
      new_action->label_translated = g_strdup(*trans ? *trans : *path);
      new_action->type = DT_ACTION_TYPE_SECTION;
      new_action->owner = owner;

      dt_action_insert_sorted(owner, new_action);

      owner = new_action;
      action = NULL;
    }
    else if(!strcmp(action->label, clean_path))
    {
      g_free(clean_path);
      owner = action;
      action = action->target;
    }
    else
    {
      action = action->next;
      continue;
    }
    clean_path = NULL; // now owned by action or freed
    path++;
    if(*trans) trans++;
  }

  g_strfreev(split_path);
  g_strfreev(split_trans);

  if(accel_key != 0)
  {
    GdkKeymap *keymap = gdk_keymap_get_for_display(gdk_display_get_default());

    GdkKeymapKey *keys;
    gint n_keys, i = 0;

    if(!gdk_keymap_get_entries_for_keyval(keymap, accel_key, &keys, &n_keys)) return;

    // find the first key in group 0, if any
    while(i < n_keys - 1 && (keys[i].group > 0 || keys[i].level > 1)) i++;

    if(keys[i].level > 1)
      fprintf(stderr, "[dt_accel_register_shortcut] expected to find a key in group 0 with only shift\n");

    if(keys[i].level == 1) mods |= GDK_SHIFT_MASK;

    if(mods & GDK_CONTROL_MASK)
    {
      mods = (mods & ~GDK_CONTROL_MASK) |
             gdk_keymap_get_modifier_mask(keymap, GDK_MODIFIER_INTENT_PRIMARY_ACCELERATOR);
    }

    dt_shortcut_t s = { .key_device = DT_SHORTCUT_DEVICE_KEYBOARD_MOUSE,
                        .mods = mods,
                        .speed = 1.0,
                        .action = owner };

    gdk_keymap_translate_keyboard_state(keymap, keys[i].keycode, 0, 0, &s.key, NULL, NULL, NULL);

    insert_shortcut(&s, FALSE);

    g_free(keys);
  }
}

void dt_accel_connect_shortcut(dt_action_t *owner, const gchar *path_string, GClosure *closure)
{
  gchar **split_path = g_strsplit(path_string, "/", 0);
  gchar **path = split_path;

  while(*path && (owner = owner->target))
  {
    gchar *clean_path = path_without_symbols(*path);

    while(owner)
    {
      if(!strcmp(owner->label, clean_path))
        break;
      else
        owner = owner->next;
    }

    g_free(clean_path);
    path++;
  }

  if(!*path && owner)
  {
    if(owner->type == DT_ACTION_TYPE_CLOSURE && owner->target)
      g_closure_unref(owner->target);

    owner->type = DT_ACTION_TYPE_CLOSURE;
    owner->target = closure;
    g_closure_ref(closure);
    g_closure_sink(closure);
  }
  else
  {
    fprintf(stderr, "[dt_accel_connect_shortcut] '%s' not found\n", path_string);
  }

  g_strfreev(split_path);
}

void dt_accel_register_global(const gchar *path, guint accel_key, GdkModifierType mods)
{
  dt_accel_register_shortcut(&darktable.control->actions_global, path, accel_key, mods);
}

void dt_accel_register_view(dt_view_t *self, const gchar *path, guint accel_key, GdkModifierType mods)
{
  dt_accel_register_shortcut(&self->actions, path, accel_key, mods);
}

void dt_accel_register_iop(dt_iop_module_so_t *so, gboolean local, const gchar *path, guint accel_key,
                           GdkModifierType mods)
{
  dt_accel_register_shortcut(&so->actions, path, accel_key, mods);
}

void dt_action_define_preset(dt_action_t *action, const gchar *name)
{
  gchar *path[3] = { "preset", (gchar *)name, NULL };
  dt_action_t *p = dt_action_locate(action, path);
  if(p)
  {
    p->type = DT_ACTION_TYPE_PRESET;
    p->target = (gpointer)TRUE;
  }
}

void dt_action_rename(dt_action_t *action, const gchar *new_name)
{
  g_free((char*)action->label);
  g_free((char*)action->label_translated);

  dt_action_t **previous = (dt_action_t **)&action->owner->target;
  while(*previous)
  {
    if(*previous == action)
    {
      *previous = action->next;
      break;
    }
    previous = &(*previous)->next;
  }

  if(new_name)
  {
    action->label = path_without_symbols(new_name);
    action->label_translated = g_strdup(_(new_name));

    dt_action_insert_sorted(action->owner, action);
  }
  else
  {
    GSequenceIter *iter = g_sequence_get_begin_iter(darktable.control->shortcuts);
    while(!g_sequence_iter_is_end(iter))
    {
      GSequenceIter *current = iter;
      iter = g_sequence_iter_next(iter); // remove will invalidate

      dt_shortcut_t *s = g_sequence_get(current);
      if(s->action == action)
        remove_shortcut(current);
    }

    if(action->type == DT_ACTION_TYPE_CLOSURE)
      g_closure_unref(action->target);

    g_free(action);
  }

  dt_shortcuts_save(FALSE);
}

void dt_action_rename_preset(dt_action_t *action, const gchar *old_name, const gchar *new_name)
{
  gchar *path[3] = { "preset", (gchar *)old_name, NULL };
  dt_action_t *p = dt_action_locate(action, path);
  if(p)
  {
    if(!new_name)
    {
      if(actions_store)
        gtk_tree_model_foreach(GTK_TREE_MODEL(actions_store), remove_from_store, p);
    }

    dt_action_rename(p, new_name);
  }
}

void dt_accel_register_lib_as_view(gchar *view_name, const gchar *path, guint accel_key, GdkModifierType mods)
{
  //register a lib shortcut but place it in the path of a view
  dt_action_t *a = darktable.control->actions_views.target;
  while(a)
  {
    if(!strcmp(a->label, view_name))
      break;
    else
      a = a->next;
  }
  if(a)
  {
    dt_accel_register_shortcut(a, path, accel_key, mods);
  }
  else
  {
    fprintf(stderr, "[dt_accel_register_lib_as_view] '%s' not found\n", view_name);
  }
}

void dt_accel_register_lib(dt_lib_module_t *self, const gchar *path, guint accel_key, GdkModifierType mods)
{
  dt_accel_register_shortcut(&self->actions, path, accel_key, mods);
}

const gchar *_common_actions[]
  = { NC_("accel", "show module"),
      NC_("accel", "enable module"),
      NC_("accel", "focus module"),
      NC_("accel", "reset module parameters"),
      NC_("accel", "show preset menu"),
      NULL };

void _accel_register_actions_iop(dt_iop_module_so_t *so, gboolean local, const gchar *path, const char **actions)
{
  for(const char **action = actions; *action; action++)
  {
    if(!path) dt_accel_register_shortcut(&so->actions, *action, 0, 0);
  }
}

void dt_accel_register_common_iop(dt_iop_module_so_t *so)
{
  _accel_register_actions_iop(so, FALSE, NULL, _common_actions);
}

void dt_accel_register_lua(const gchar *path, guint accel_key, GdkModifierType mods)
{
  dt_accel_register_shortcut(&darktable.control->actions_lua, path, accel_key, mods);
}

void dt_accel_register_manual(const gchar *full_path, dt_view_type_flags_t views, guint accel_key,
                              GdkModifierType mods)
{
  gchar **split_path = g_strsplit(full_path, "/", 3);
  if(!strcmp(split_path[0], "views") && !strcmp(split_path[1], "thumbtable"))
    dt_accel_register_shortcut(&darktable.control->actions_thumb, split_path[2], accel_key, mods);
  g_strfreev(split_path);
}

void dt_accel_connect_global(const gchar *path, GClosure *closure)
{
  dt_accel_connect_shortcut(&darktable.control->actions_global, path, closure);
}

void dt_accel_connect_view(dt_view_t *self, const gchar *path, GClosure *closure)
{
  dt_accel_connect_shortcut(&self->actions, path, closure);
}

void dt_accel_connect_lib_as_view(dt_lib_module_t *module, gchar *view_name, const gchar *path, GClosure *closure)
{
  dt_action_t *a = darktable.control->actions_views.target;
  while(a)
  {
    if(!strcmp(a->label, view_name))
      break;
    else
      a = a->next;
  }
  if(a)
  {
    dt_accel_connect_shortcut(a, path, closure);
  }
  else
  {
    fprintf(stderr, "[dt_accel_register_lib_as_view] '%s' not found\n", view_name);
  }
}

void dt_accel_connect_lib_as_global(dt_lib_module_t *module, const gchar *path, GClosure *closure)
{
  dt_accel_connect_shortcut(&darktable.control->actions_global, path, closure);
}

void dt_accel_connect_iop(dt_iop_module_t *module, const gchar *path, GClosure *closure)
{
  gchar **split_path = g_strsplit(path, "`", 6);
  dt_action_t *ac = dt_action_locate(&module->so->actions, split_path);
  g_strfreev(split_path);

  if(ac)
  {
    ac->type = DT_ACTION_TYPE_CLOSURE;

    // to support multi-instance, save in and own by per instance widget list
    dt_action_target_t *referral = g_malloc0(sizeof(dt_action_target_t));
    referral->action = ac;
    referral->target = closure;
    g_closure_ref(closure);
    g_closure_sink(closure);
    module->widget_list = g_slist_prepend(module->widget_list, referral);
  }
}

void dt_accel_connect_lib(dt_lib_module_t *module, const gchar *path, GClosure *closure)
{
  dt_accel_connect_shortcut(&module->actions, path, closure);
}

void dt_accel_connect_lua(const gchar *path, GClosure *closure)
{
  dt_accel_connect_shortcut(&darktable.control->actions_lua, path, closure);
}

void dt_accel_connect_manual(GSList **list_ptr, const gchar *full_path, GClosure *closure)
{
  gchar **split_path = g_strsplit(full_path, "/", 3);
  if(!strcmp(split_path[0], "views") && !strcmp(split_path[1], "thumbtable"))
    dt_accel_connect_shortcut(&darktable.control->actions_thumb, split_path[2], closure);
  g_strfreev(split_path);
}

void dt_accel_connect_button_iop(dt_iop_module_t *module, const gchar *path, GtkWidget *button)
{
  dt_action_define_iop(module, path, FALSE, 0, 0, button);
}

void dt_accel_connect_button_lib(dt_lib_module_t *module, const gchar *path, GtkWidget *button)
{
  _action_define(&module->actions, path, FALSE, 0, 0, button);
}

void dt_accel_connect_button_lib_as_global(dt_lib_module_t *module, const gchar *path, GtkWidget *button)
{
  _action_define(&darktable.control->actions_global, path, FALSE, 0, 0, button);
}

void dt_accel_widget_toast(GtkWidget *widget)
{
  dt_bauhaus_widget_t *w = (dt_bauhaus_widget_t *)DT_BAUHAUS_WIDGET(widget);

  if(!darktable.gui->reset)
  {
    char *text = NULL;

    switch(w->type){
      case DT_BAUHAUS_SLIDER:
      {
        text = dt_bauhaus_slider_get_text(widget);
        break;
      }
      case DT_BAUHAUS_COMBOBOX:
        text = g_strdup(dt_bauhaus_combobox_get_text(widget));
        break;
      default: //literally impossible but hey
        return;
        break;
    }

    if(w->label[0] != '\0')
    { // label is not empty
      if(w->module && w->module->multi_name[0] != '\0')
        dt_toast_log(_("%s %s / %s: %s"), w->module->name(), w->module->multi_name, w->label, text);
      else if(w->module && !strstr(w->module->name(), w->label))
        dt_toast_log(_("%s / %s: %s"), w->module->name(), w->label, text);
      else
        dt_toast_log(_("%s: %s"), w->label, text);
    }
    else
    { //label is empty
      if(w->module && w->module->multi_name[0] != '\0')
        dt_toast_log(_("%s %s / %s"), w->module->name(), w->module->multi_name, text);
      else if(w->module)
        dt_toast_log(_("%s / %s"), w->module->name(), text);
      else
        dt_toast_log("%s", text);
    }

    g_free(text);
  }

}

float dt_accel_get_slider_scale_multiplier()
{
  const int slider_precision = dt_conf_get_int("accel/slider_precision");

  if(slider_precision == DT_IOP_PRECISION_COARSE)
  {
    return dt_conf_get_float("darkroom/ui/scale_rough_step_multiplier");
  }
  else if(slider_precision == DT_IOP_PRECISION_FINE)
  {
    return dt_conf_get_float("darkroom/ui/scale_precise_step_multiplier");
  }

  return dt_conf_get_float("darkroom/ui/scale_step_multiplier");
}

void dt_accel_connect_instance_iop(dt_iop_module_t *module)
{
  for(GSList *w = module->widget_list; w; w = w->next)
  {
    dt_action_target_t *referral = w->data;
    referral->action->target = referral->target;
  }
}

void _destroy_referral(gpointer data)
{
  dt_action_target_t *referral = data;
  if(referral->action && referral->action->type == DT_ACTION_TYPE_CLOSURE)
  {
    if(referral->action->target == referral->target)
      referral->action->target = NULL;
    g_closure_unref(referral->target);
  }

  g_free(referral);
}

// FIXME rename to dt_actions_cleanup_instance_iop
void dt_accel_cleanup_closures_iop(dt_iop_module_t *module)
{
  g_slist_free_full(module->widget_list, _destroy_referral);
}

void dt_accel_rename_global(const gchar *path, const gchar *new_name)
{
  gchar **split_path = g_strsplit(path, "/", 6);
  dt_action_t *p = dt_action_locate(&darktable.control->actions_global, split_path);
  g_strfreev(split_path);

  if(p) dt_action_rename(p, new_name);
}

void dt_accel_rename_lua(const gchar *path, const gchar *new_name)
{
  gchar **split_path = g_strsplit(path, "/", 6);
  dt_action_t *p = dt_action_locate(&darktable.control->actions_lua, split_path);
  g_strfreev(split_path);

  if(p) dt_action_rename(p, new_name);
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
