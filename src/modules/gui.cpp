/* ASE - Allegro Sprite Editor
 * Copyright (C) 2001-2009  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <list>
#include <vector>
#include <cassert>
#include <algorithm>
#include <allegro.h>
#include <allegro/internal/aintern.h>

#ifdef ALLEGRO_WINDOWS
#include <winalleg.h>
#endif

#include "jinete/jinete.h"
#include "jinete/jintern.h"

#include "ui_context.h"
#include "commands/command.h"
#include "commands/commands.h"
#include "commands/params.h"
#include "console.h"
#include "core/app.h"
#include "core/cfg.h"
#include "core/core.h"
#include "core/dirs.h"
#include "core/drop_files.h"
#include "dialogs/options.h"
#include "intl/msgids.h"
#include "modules/editors.h"
#include "modules/gfx.h"
#include "modules/gui.h"
#include "modules/palettes.h"
#include "modules/rootmenu.h"
#include "modules/tools.h"
#include "raster/sprite.h"
#include "util/recscr.h"
#include "widgets/editor.h"
#include "widgets/statebar.h"
#include "sprite_wrappers.h"

#define REBUILD_RECENT_LIST	2
#define REFRESH_FULL_SCREEN	4

#define MONITOR_TIMER_MSECS	100

//////////////////////////////////////////////////////////////////////

#ifdef ALLEGRO_WINDOWS
#  define DEF_SCALE 2
#else
#  define DEF_SCALE 1
#endif

static struct
{
  int width;
  int height;
  int scale;
} try_resolutions[] = { { 1024, 768, DEF_SCALE },
			{  800, 600, DEF_SCALE },
			{  640, 480, DEF_SCALE },
			{  320, 240, 1 },
			{  320, 200, 1 },
			{    0,   0, 0 } };

static int try_depths[] = { 32, 24, 16, 15, 8 };

//////////////////////////////////////////////////////////////////////

enum ShortcutType { Shortcut_ExecuteCommand,
		    Shortcut_ChangeTool };

struct Shortcut
{
  JAccel accel;
  ShortcutType type;
  union {
    Command* command;
    Tool* tool;
  };
  Params* params;

  Shortcut(ShortcutType type);
  ~Shortcut();

  void add_shortcut(const char* shortcut_string);
  bool is_key_pressed(JMessage msg);

};

static Shortcut* get_keyboard_shortcut_for_command(const char* command_name, Params* params);
static Shortcut* get_keyboard_shortcut_for_tool(Tool* tool);

//////////////////////////////////////////////////////////////////////

struct Monitor
{
  // returns true when the job is done and the monitor can be removed
  void (*proc)(void *);
  void (*free)(void *);
  void *data;
  bool lock;
  bool deleted;

  Monitor(void (*proc)(void *),
	  void (*free)(void *), void *data);
  ~Monitor();
};

//////////////////////////////////////////////////////////////////////

static JWidget manager = NULL;

static int monitor_timer = -1;
static MonitorList* monitors = NULL;
static std::vector<Shortcut*>* shortcuts = NULL;

static bool ji_screen_created = FALSE;

static volatile int next_idle_flags = 0;
static JList icon_buttons;

/* default GUI screen configuration */
static bool double_buffering;
static int screen_scaling;

/* load & save graphics configuration */
static void load_gui_config(int *w, int *h, int *bpp, bool *fullscreen);
static void save_gui_config();

static bool button_with_icon_msg_proc(JWidget widget, JMessage msg);
static bool manager_msg_proc(JWidget widget, JMessage msg);

static void regen_theme_and_fixup_icons(void *data);

/**
 * Used by set_display_switch_callback(SWITCH_IN, ...).
 */
static void display_switch_in_callback()
{
  next_idle_flags |= REFRESH_FULL_SCREEN;
}

END_OF_STATIC_FUNCTION(display_switch_in_callback);

/**
 * Initializes GUI.
 */
int init_module_gui()
{
  int min_possible_dsk_res = 0;
  int c, w, h, bpp, autodetect;
  bool fullscreen;

  monitors = new MonitorList;
  shortcuts = new std::vector<Shortcut*>;

  /* install the mouse */
  if (install_mouse() < 0) {
    user_printf(_("Error installing mouse handler\n"));
    return -1;
  }

  /* install the keyboard */
  if (install_keyboard() < 0) {
    user_printf(_("Error installing keyboard handler\n"));
    return -1;
  }

  /* disable Ctrl+Shift+End in non-DOS */
#if !defined(ALLEGRO_DOS)
  three_finger_flag = FALSE;
#endif
  three_finger_flag = TRUE;	/* TODO remove this line */

  /* set the graphics mode... */
  load_gui_config(&w, &h, &bpp, &fullscreen);

  autodetect = fullscreen ? GFX_AUTODETECT_FULLSCREEN:
			    GFX_AUTODETECT_WINDOWED;

  /* default resolution */
  if (!w || !h) {
    bool has_desktop = FALSE;
    int dsk_w, dsk_h;

    has_desktop = get_desktop_resolution(&dsk_w, &dsk_h) == 0;

    /* we must extract some space for the windows borders */
    dsk_w -= 16;
    dsk_h -= 32;

    /* try to get desktop resolution */
    if (has_desktop) {
      for (c=0; try_resolutions[c].width; ++c) {
	if (try_resolutions[c].width <= dsk_w &&
	    try_resolutions[c].height <= dsk_h) {
	  min_possible_dsk_res = c;
	  fullscreen = FALSE;
	  w = try_resolutions[c].width;
	  h = try_resolutions[c].height;
	  screen_scaling = try_resolutions[c].scale;
	  break;
	}
      }
    }
    /* full screen */
    else {
      fullscreen = TRUE;
      w = 320;
      h = 200;
      screen_scaling = 1;
    }
  }

  /* default color depth */
  if (!bpp) {
    bpp = desktop_color_depth();
    if (!bpp)
      bpp = 8;
  }

  for (;;) {
    /* original */
    set_color_depth(bpp);
    if (set_gfx_mode(autodetect, w, h, 0, 0) == 0)
      break;

    for (c=min_possible_dsk_res; try_resolutions[c].width; ++c) {
      if (set_gfx_mode(autodetect,
		       try_resolutions[c].width,
		       try_resolutions[c].height, 0, 0) == 0) {
	screen_scaling = try_resolutions[c].scale;
	goto gfx_done;
      }
    }

    if (bpp == 8) {
      user_printf(_("Error setting graphics mode\n%s\n"
		    "Try \"ase -res WIDTHxHEIGHTxBPP\"\n"), allegro_error);
      return -1;
    }
    else {
      for (c=0; try_depths[c]; ++c) {
	if (bpp == try_depths[c]) {
	  bpp = try_depths[c+1];
	  break;
	}
      }
    }
  }
 gfx_done:;

  /* window title */
  set_window_title("Allegro Sprite Editor v" VERSION);

  /* create the default-manager */
  manager = jmanager_new();
  jwidget_add_hook(manager, JI_WIDGET, manager_msg_proc, NULL);

  /* setup the standard jinete theme for widgets */
  ji_set_standard_theme();

  /* set hook to translate strings */
  ji_set_translation_hook(msgids_get);

  /* configure ji_screen */
  gui_setup_screen();

  /* add a hook to display-switch so when the user returns to the
     screen it's completelly refreshed/redrawn */
  LOCK_VARIABLE(next_idle_flags);
  LOCK_FUNCTION(display_switch_in_callback);
  set_display_switch_callback(SWITCH_IN, display_switch_in_callback);

  /* set graphics options for next time */
  save_gui_config();

  /* load the font */
  reload_default_font();

  /* hook for palette change to regenerate the theme */
  app_add_hook(APP_PALETTE_CHANGE, regen_theme_and_fixup_icons, NULL);

  /* icon buttons */
  icon_buttons = jlist_new();

  /* setup mouse */
  _setup_mouse_speed();

  return 0;
}

void exit_module_gui()
{
  // destroy shortcuts
  assert(shortcuts != NULL);
  for (std::vector<Shortcut*>::iterator
	 it = shortcuts->begin(); it != shortcuts->end(); ++it) {
    Shortcut* shortcut = *it;
    delete shortcut;
  }
  delete shortcuts;
  shortcuts = NULL;

  // destroy monitors
  assert(monitors != NULL);
  for (MonitorList::iterator
  	 it2 = monitors->begin(); it2 != monitors->end(); ++it2) {
    Monitor* monitor = *it2;
    delete monitor;
  }
  delete monitors;
  monitors = NULL;

  if (double_buffering) {
    BITMAP *old_bmp = ji_screen;
    ji_set_screen(screen);

    if (ji_screen_created)
      destroy_bitmap(old_bmp);
    ji_screen_created = FALSE;
  }

  jlist_free(icon_buttons);
  icon_buttons = NULL;

  jmanager_free(manager);

  remove_keyboard();
  remove_mouse();
}

int guiscale()
{
  return (JI_SCREEN_W > 512 ? 2: 1);
}

Monitor::Monitor(void (*proc)(void *),
		 void (*free)(void *), void *data)
{
  this->proc = proc;
  this->free = free;
  this->data = data;
  this->lock = false;
  this->deleted = false;
}

Monitor::~Monitor()
{
  if (this->free)
    (*this->free)(this->data);
}

static void load_gui_config(int *w, int *h, int *bpp, bool *fullscreen)
{
  *w = get_config_int("GfxMode", "Width", 0);
  *h = get_config_int("GfxMode", "Height", 0);
  *bpp = get_config_int("GfxMode", "Depth", 0);
  *fullscreen = get_config_bool("GfxMode", "FullScreen", FALSE);
  screen_scaling = get_config_int("GfxMode", "Scale", 1);
  screen_scaling = MID(1, screen_scaling, 4);
}

static void save_gui_config()
{
  set_config_int("GfxMode", "Width", SCREEN_W);
  set_config_int("GfxMode", "Height", SCREEN_H);
  set_config_int("GfxMode", "Depth", bitmap_color_depth(screen));
  set_config_bool("GfxMode", "FullScreen", gfx_driver->windowed ? false: true);
  set_config_int("GfxMode", "Scale", screen_scaling);
}

int get_screen_scaling()
{
  return screen_scaling;
}

void set_screen_scaling(int scaling)
{
  screen_scaling = scaling;
}

void update_screen_for_sprite(const Sprite* sprite)
{
  if (!(ase_mode & MODE_GUI))
    return;

  /* without sprite */
  if (!sprite) {
    /* well, change to the default palette */
    if (set_current_palette(NULL, FALSE)) {
      /* if the palette changes, refresh the whole screen */
      jmanager_refresh_screen();
    }
  }
  /* with a sprite */
  else {
    /* select the palette of the sprite */
    if (set_current_palette(sprite_get_palette(sprite, sprite->frame), FALSE)) {
      /* if the palette changes, refresh the whole screen */
      jmanager_refresh_screen();
    }
    else {
      /* if it's the same palette update only the editors with the sprite */
      update_editors_with_sprite(sprite);
    }
  }

  statusbar_set_text(app_get_statusbar(), -1, "");
}

void gui_run()
{
  jmanager_run(manager);
}

void gui_feedback()
{
  /* menu stuff */
  if (next_idle_flags & REBUILD_RECENT_LIST) {
    if (app_realloc_recent_list())
      next_idle_flags ^= REBUILD_RECENT_LIST;
  }

  if (next_idle_flags & REFRESH_FULL_SCREEN) {
    next_idle_flags ^= REFRESH_FULL_SCREEN;

    const CurrentSpriteReader sprite(UIContext::instance());
    update_screen_for_sprite(sprite);
  }

  /* record file if is necessary */
  rec_screen_poll();

  /* double buffering? */
  if (double_buffering) {
    jmouse_draw_cursor();

    if (ji_dirty_region) {
      ji_flip_dirty_region();
    }
    else {
      if (JI_SCREEN_W == SCREEN_W && JI_SCREEN_H == SCREEN_H) {
	blit(ji_screen, screen, 0, 0, 0, 0, SCREEN_W, SCREEN_H);
      }
      else {
	stretch_blit(ji_screen, screen,
		     0, 0, ji_screen->w, ji_screen->h,
		     0, 0, SCREEN_W, SCREEN_H);
      }
    }
  }
}

/**
 * Sets the ji_screen variable.
 * 
 * This routine should be called everytime you changes the graphics
 * mode.
 */
void gui_setup_screen()
{
  /* double buffering is required when screen scaling is used */
  double_buffering = (screen_scaling > 1);

  /* is double buffering active */
  if (double_buffering) {
    BITMAP *old_bmp = ji_screen;
    ji_set_screen(create_bitmap(SCREEN_W / screen_scaling,
				SCREEN_H / screen_scaling));
    if (ji_screen_created)
      destroy_bitmap(old_bmp);

    ji_screen_created = TRUE;
  }
  else {
    ji_set_screen(screen);
    ji_screen_created = FALSE;
  }

  reload_default_font();

  /* set the configuration */
  save_gui_config();
}

void reload_default_font()
{
  JTheme theme = ji_get_theme();
  const char *user_font;
  DIRS *dirs, *dir;
  char buf[512];

  /* no font for now */

  if (theme->default_font && theme->default_font != font)
    destroy_font(theme->default_font);

  theme->default_font = NULL;

  /* directories */
  dirs = dirs_new();

  user_font = get_config_string("Options", "UserFont", "");
  if ((user_font) && (*user_font))
    dirs_add_path(dirs, user_font);

  usprintf(buf, "fonts/ase%d.pcx", guiscale());
  dirs_cat_dirs(dirs, filename_in_datadir(buf));

  /* try to load the font */
  for (dir=dirs; dir; dir=dir->next) {
    theme->default_font = ji_font_load(dir->path);
    if (theme->default_font) {
      if (ji_font_is_scalable(theme->default_font))
	ji_font_set_size(theme->default_font, 8*guiscale());
      break;
    }
  }

  dirs_free(dirs);

  /* default font: the Allegro one */

  if (!theme->default_font)
    theme->default_font = font;

  /* set all widgets fonts */
  _ji_set_font_of_all_widgets(theme->default_font);
}

void load_window_pos(JWidget window, const char *section)
{
  JRect pos, orig_pos;

  /* default position */
  orig_pos = jwidget_get_rect(window);
  pos = jrect_new_copy(orig_pos);

  /* load configurated position */
  get_config_rect(section, "WindowPos", pos);

  pos->x2 = pos->x1 + MID(jrect_w(orig_pos), jrect_w(pos), JI_SCREEN_W);
  pos->y2 = pos->y1 + MID(jrect_h(orig_pos), jrect_h(pos), JI_SCREEN_H);

  jrect_moveto(pos,
	       MID(0, pos->x1, JI_SCREEN_W-jrect_w(pos)),
	       MID(0, pos->y1, JI_SCREEN_H-jrect_h(pos)));

  jwidget_set_rect(window, pos);

  jrect_free(pos);
  jrect_free(orig_pos);
}

void save_window_pos(JWidget window, const char *section)
{
  set_config_rect(section, "WindowPos", window->rc);
}

JWidget load_widget(const char *filename, const char *name)
{
  JWidget widget;
  DIRS *it, *dirs;
  char buf[512];
  bool found = false;

  dirs = dirs_new();

  usprintf(buf, "jids/%s", filename);

  dirs_add_path(dirs, filename);
  dirs_cat_dirs(dirs, filename_in_datadir(buf));

  for (it=dirs; it; it=it->next) {
    if (exists(it->path)) {
      ustrcpy(buf, it->path);
      found = true;
      break;
    }
  }

  dirs_free(dirs);

  if (!found)
    throw widget_file_not_found(filename);

  widget = ji_load_widget(buf, name);
  if (!widget)
    throw widget_not_found(name);

  return widget;
}

JWidget find_widget(JWidget widget, const char *name)
{
  JWidget child = jwidget_find_name(widget, name);
  if (!child)
    throw widget_not_found(name);

  return child;
}

void schedule_rebuild_recent_list()
{
  next_idle_flags |= REBUILD_RECENT_LIST;
}

/**********************************************************************/
/* hook signals */

typedef struct HookData {
  int signal_num;
  bool (*signal_handler)(JWidget widget, void *data);
  void *data;
} HookData;

static int hook_type()
{
  static int type = 0;
  if (!type)
    type = ji_register_widget_type();
  return type;
}

static bool hook_msg_proc(JWidget widget, JMessage msg)
{
  switch (msg->type) {

    case JM_DESTROY:
      jfree(jwidget_get_data(widget, hook_type()));
      break;

    case JM_SIGNAL: {
      HookData* hook_data = reinterpret_cast<HookData*>(jwidget_get_data(widget, hook_type()));
      if (hook_data &&
	  hook_data->signal_num == msg->signal.num) {
	return (*hook_data->signal_handler)(widget, hook_data->data);
      }
      break;
    }
  }

  return FALSE;
}

/**
 * @warning You can't use this function for the same widget two times.
 */
void hook_signal(JWidget widget,
		 int signal_num,
		 bool (*signal_handler)(JWidget widget, void* data),
		 void* data)
{
  HookData* hook_data = jnew(HookData, 1);

  hook_data->signal_num = signal_num;
  hook_data->signal_handler = signal_handler;
  hook_data->data = data;

  jwidget_add_hook(widget, hook_type(), hook_msg_proc, hook_data);
}

/**
 * Utility routine to get various widgets by name.
 *
 * @code
 * if (!get_widgets(wnd,
 *                  "name1", &widget1,
 *                  "name2", &widget2,
 *                  "name3", &widget3,
 *                  NULL)) {
 *   ...
 * }
 * @endcode
 */
void get_widgets(JWidget window, ...)
{
  JWidget *widget;
  char *name;
  va_list ap;

  va_start(ap, window);
  while ((name = va_arg(ap, char *))) {
    widget = va_arg(ap, JWidget *);
    if (!widget)
      break;

    *widget = jwidget_find_name(window, name);
    if (!*widget)
      throw widget_not_found(name);
  }
  va_end(ap);
}

/**********************************************************************/
/* Icon in buttons */

/* adds a button in the list of "icon_buttons" to restore the icon
   when the palette changes, the "user_data[3]" is used to save the
   "gfx_id" (the same ID that is used in "get_gfx()" from
   "modules/gfx.c"), also this routine adds a hook to
   JI_SIGNAL_DESTROY to remove the button from the "icon_buttons"
   list when the widget is free */
void add_gfxicon_to_button(JWidget button, int gfx_id, int icon_align)
{
  button->user_data[3] = (void *)gfx_id;

  jwidget_add_hook(button, JI_WIDGET, button_with_icon_msg_proc, NULL);

  ji_generic_button_set_icon(button, get_gfx(gfx_id));
  ji_generic_button_set_icon_align(button, icon_align);

  jlist_append(icon_buttons, button);
}

void set_gfxicon_in_button(JWidget button, int gfx_id)
{
  button->user_data[3] = (void *)gfx_id;

  ji_generic_button_set_icon(button, get_gfx(gfx_id));

  jwidget_dirty(button);
}

static bool button_with_icon_msg_proc(JWidget widget, JMessage msg)
{
  if (msg->type == JM_DESTROY)
    jlist_remove(icon_buttons, widget);
  return FALSE;
}

/**********************************************************************/
/* Button style (convert radio or check buttons and draw it like
   normal buttons) */

JWidget radio_button_new(int radio_group, int b1, int b2, int b3, int b4)
{
  JWidget widget = ji_generic_button_new(NULL, JI_RADIO, JI_BUTTON);
  if (widget) {
    jradio_set_group(widget, radio_group);
    jbutton_set_bevel(widget, b1, b2, b3, b4);
  }
  return widget;
}

JWidget check_button_new(const char *text, int b1, int b2, int b3, int b4)
{
  JWidget widget = ji_generic_button_new(text, JI_CHECK, JI_BUTTON);
  if (widget) {
    jbutton_set_bevel(widget, b1, b2, b3, b4);
  }
  return widget;
}

//////////////////////////////////////////////////////////////////////
// Keyboard shortcuts
//////////////////////////////////////////////////////////////////////

JAccel add_keyboard_shortcut_to_execute_command(const char* shortcut_string, const char* command_name, Params* params)
{
  Shortcut* shortcut = get_keyboard_shortcut_for_command(command_name, params);

  if (!shortcut) {
    shortcut = new Shortcut(Shortcut_ExecuteCommand);
    shortcut->command = CommandsModule::instance()->get_command_by_name(command_name);
    shortcut->params = params ? params->clone(): new Params;

    shortcuts->push_back(shortcut);
  }

  shortcut->add_shortcut(shortcut_string);
  return shortcut->accel;
}

JAccel add_keyboard_shortcut_to_change_tool(const char* shortcut_string, Tool* tool)
{
  Shortcut* shortcut = get_keyboard_shortcut_for_tool(tool);

  if (!shortcut) {
    shortcut = new Shortcut(Shortcut_ChangeTool);
    shortcut->tool = tool;

    shortcuts->push_back(shortcut);
  }

  shortcut->add_shortcut(shortcut_string);
  return shortcut->accel;
}

Command* get_command_from_key_message(JMessage msg)
{
  for (std::vector<Shortcut*>::iterator
	 it = shortcuts->begin(); it != shortcuts->end(); ++it) {
    Shortcut* shortcut = *it;

    if (shortcut->type == Shortcut_ExecuteCommand &&
	// TODO why?
	// shortcut->argument.empty() &&
	shortcut->is_key_pressed(msg)) {
      return shortcut->command;
    }
  }
  return NULL;
}

JAccel get_accel_to_execute_command(const char* command_name, Params* params)
{
  Shortcut* shortcut = get_keyboard_shortcut_for_command(command_name, params);
  if (shortcut)
    return shortcut->accel;
  else
    return NULL;
}

JAccel get_accel_to_change_tool(Tool* tool)
{
  Shortcut* shortcut = get_keyboard_shortcut_for_tool(tool);
  if (shortcut)
    return shortcut->accel;
  else
    return NULL;
}

Shortcut::Shortcut(ShortcutType type)
{
  this->type = type;
  this->accel = jaccel_new();
  this->command = NULL;
  this->tool = NULL;
  this->params = NULL;
}

Shortcut::~Shortcut()
{
  delete params;
  jaccel_free(accel);
}

void Shortcut::add_shortcut(const char* shortcut_string)
{
  char buf[256];
  usprintf(buf, "<%s>", shortcut_string);
  jaccel_add_keys_from_string(this->accel, buf);
}

bool Shortcut::is_key_pressed(JMessage msg)
{
  if (accel) {
    return jaccel_check(accel,
			msg->any.shifts,
			msg->key.ascii,
			msg->key.scancode);
  }
  return false;
}

static Shortcut* get_keyboard_shortcut_for_command(const char* command_name, Params* params)
{
  Command* command = CommandsModule::instance()->get_command_by_name(command_name);
  if (!command)
    return NULL;

  for (std::vector<Shortcut*>::iterator
	 it = shortcuts->begin(); it != shortcuts->end(); ++it) {
    Shortcut* shortcut = *it;

    if (shortcut->type == Shortcut_ExecuteCommand &&
	shortcut->command == command &&
	((!params && shortcut->params->empty()) ||
	 (params && *shortcut->params == *params))) {
      return shortcut;
    }
  }

  return NULL;
}

static Shortcut* get_keyboard_shortcut_for_tool(Tool* tool)
{
  for (std::vector<Shortcut*>::iterator
	 it = shortcuts->begin(); it != shortcuts->end(); ++it) {
    Shortcut* shortcut = *it;

    if (shortcut->type == Shortcut_ChangeTool &&
	shortcut->tool == tool) {
      return shortcut;
    }
  }

  return NULL;
}

/**
 * Adds a routine to be called each 100 milliseconds to monitor
 * whatever you want. It's mainly used to monitor the progress of a
 * file-operation (see @ref fop_operate)
 */
Monitor* add_gui_monitor(void (*proc)(void *),
			 void (*free)(void *), void *data)
{
  Monitor* monitor = new Monitor(proc, free, data);

  monitors->push_back(monitor);

  if (monitor_timer < 0)
    monitor_timer = jmanager_add_timer(manager, MONITOR_TIMER_MSECS);

  jmanager_start_timer(monitor_timer);

  return monitor;
}

/**
 * Removes and frees a previously added monitor.
 */
void remove_gui_monitor(Monitor* monitor)
{
  MonitorList::iterator it =
    std::find(monitors->begin(), monitors->end(), monitor);

  assert(it != monitors->end());

  if (!monitor->lock)
    delete monitor;
  else
    monitor->deleted = true;

  monitors->erase(it);
  if (monitors->empty())
    jmanager_stop_timer(monitor_timer);
}

void* get_monitor_data(Monitor* monitor)
{
  return monitor->data;
}

/**********************************************************************/
/* manager event handler */

static bool manager_msg_proc(JWidget widget, JMessage msg)
{
  switch (msg->type) {

    case JM_QUEUEPROCESSING:
      gui_feedback();

      /* open dropped files */
      check_for_dropped_files();
      break;

    case JM_TIMER:
      if (msg->timer.timer_id == monitor_timer) {
	for (MonitorList::iterator
	       it = monitors->begin(), next; it != monitors->end(); it = next) {
	  Monitor* monitor = *it;
	  next = it;
	  ++next;

	  // is the monitor not lock?
	  if (!monitor->lock) {
	    // call the monitor procedure
	    monitor->lock = true;
	    (*monitor->proc)(monitor->data);
	    monitor->lock = false;

	    if (monitor->deleted)
	      delete monitor;
	  }
	}

	// is monitors empty? we can stop the timer so
	if (monitors->empty())
	  jmanager_stop_timer(monitor_timer);
      }
      break;

    case JM_KEYPRESSED:
      for (std::vector<Shortcut*>::iterator
	     it = shortcuts->begin(); it != shortcuts->end(); ++it) {
	Shortcut* shortcut = *it;

	if (shortcut->is_key_pressed(msg)) {
	  switch (shortcut->type) {

	    case Shortcut_ChangeTool: {
	      Tool* select_this_tool = shortcut->tool;
	      Tool* group[MAX_TOOLS];
	      int i, j;

	      for (i=j=0; i<MAX_TOOLS; i++) {
		if (get_keyboard_shortcut_for_tool(tools_list[i])->is_key_pressed(msg))
		  group[j++] = tools_list[i];
	      }

	      if (j >= 2) {
		for (i=0; i<j; i++) {
		  if (group[i] == current_tool && i+1 < j) {
		    select_this_tool = group[i+1];
		    break;
		  }
		}
	      }

	      select_tool(select_this_tool);
	      break;
	    }

	    case Shortcut_ExecuteCommand: {
	      Command* command = shortcut->command;

	      // the screen shot is available in everywhere
	      if (strcmp(command->short_name(), CommandId::screen_shot) == 0) {
		UIContext::instance()->execute_command(command, shortcut->params);
		return true;
	      }
	      // all other keys are only available in the main-window
	      else {
		JWidget child;
		JLink link;

		JI_LIST_FOR_EACH(widget->children, link) {
		  child = reinterpret_cast<JWidget>(link->data);

		  /* there are a foreground window executing? */
		  if (jwindow_is_foreground(child)) {
		    break;
		  }
		  /* is it the desktop and the top-window= */
		  else if (jwindow_is_desktop(child) && child == app_get_top_window()) {
		    /* ok, so we can execute the command represented by the
		       pressed-key in the message... */
		    UIContext::instance()->execute_command(command, shortcut->params);
		    return true;
		  }
		}
	      }
	      break;
	    }

	  }
	  break;
	}
      }
      break;

  }

  return false;
}

/**********************************************************************/
/* graphics */

static void regen_theme_and_fixup_icons(void *data)
{
  JWidget button;
  JLink link;

  /* regenerate the theme */
  ji_regen_theme();

  /* fixup the icons */
  JI_LIST_FOR_EACH(icon_buttons, link) {
    button = reinterpret_cast<JWidget>(link->data);
    ji_generic_button_set_icon(button, get_gfx((size_t)button->user_data[3]));
  }
}
