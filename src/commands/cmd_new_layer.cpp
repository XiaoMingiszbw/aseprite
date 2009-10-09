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

#include "jinete/jinete.h"

#include "commands/command.h"
#include "core/app.h"
#include "modules/gui.h"
#include "raster/layer.h"
#include "raster/sprite.h"
#include "undoable.h"
#include "sprite_wrappers.h"

//////////////////////////////////////////////////////////////////////
// new_layer

class NewLayerCommand : public Command
{
public:
  NewLayerCommand();
  Command* clone() { return new NewLayerCommand(*this); }

protected:
  bool enabled(Context* context);
  void execute(Context* context);
};

static char* get_unique_layer_name(Sprite* sprite);
static int get_max_layer_num(Layer* layer);

NewLayerCommand::NewLayerCommand()
  : Command("new_layer",
	    "New Layer",
	    CmdRecordableFlag)
{
}

bool NewLayerCommand::enabled(Context* context)
{
  const CurrentSpriteReader sprite(context);
  return
    sprite != NULL;
}

void NewLayerCommand::execute(Context* context)
{
  CurrentSpriteWriter sprite(context);

  JWidgetPtr window(load_widget("newlay.jid", "new_layer"));
  JWidget name_widget = find_widget(window, "name");
  {
    char* name = get_unique_layer_name(sprite);
    jwidget_set_text(name_widget, name);
    jfree(name);
  }
  jwidget_set_min_size(name_widget, 128, 0);

  jwindow_open_fg(window);

  if (jwindow_get_killer(window) == jwidget_find_name(window, "ok")) {
    const char* name = jwidget_get_text(jwidget_find_name(window, "name"));
    Layer* layer;
    {
      Undoable undoable(sprite, "New Layer");
      layer = undoable.new_layer();
      undoable.commit();
    }
    layer_set_name(layer, name);
    update_screen_for_sprite(sprite);
  }
}

static char* get_unique_layer_name(Sprite* sprite)
{
  if (sprite != NULL) {
    char buf[1024];
    sprintf(buf, "Layer %d", get_max_layer_num(sprite->set)+1);
    return jstrdup(buf);
  }
  else
    return NULL;
}

static int get_max_layer_num(Layer* layer)
{
  int max = 0;

  if (strncmp(layer->name, "Layer ", 6) == 0)
    max = strtol(layer->name+6, NULL, 10);

  if (layer_is_set(layer)) {
    JLink link;
    JI_LIST_FOR_EACH(layer->layers, link) {
      int tmp = get_max_layer_num(reinterpret_cast<Layer*>(link->data));
      max = MAX(tmp, max);
    }
  }
  
  return max;
}

//////////////////////////////////////////////////////////////////////
// CommandFactory

Command* CommandFactory::create_new_layer_command()
{
  return new NewLayerCommand;
}
