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

#include <allegro.h>
#include <stdio.h>

#include "ase_exception.h"
#include "core/app.h"

//////////////////////////////////////////////////////////////////////
// Information for "ident".

const char ase_ident[] =
    "$ASE: " VERSION " " COPYRIGHT " $\n"
    "$Website: " WEBSITE " $\n";

//////////////////////////////////////////////////////////////////////
// Allegro libray initialization

class Allegro {
public:
  Allegro() {
    allegro_init();
    set_uformat(U_ASCII);
    install_timer();
  }
  ~Allegro() {
    remove_timer();
    allegro_exit();
  }
};

/**
 * Here ASE starts.
 */
int main(int argc, char *argv[])
{
  try {
    Allegro allegro;
    try {
      Jinete jinete;
      App app(argc, argv);

      app.run();
    }
    catch (std::exception& e) {
      allegro_message(e.what());
    }
    catch (...) {
      allegro_message("Uncaught exception");
    }
  }
  catch (...) {
    printf("Uncaught exception");
  }
  return 0;
}

END_OF_MAIN();
