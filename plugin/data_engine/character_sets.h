/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef PLUGIN_DATA_ENGINE_CHARACTER_SETS_H
#define PLUGIN_DATA_ENGINE_CHARACTER_SETS_H

#include "config.h"

#include "drizzled/plugin/table_function.h"
#include "drizzled/field.h"

class CharacterSetsTool : public drizzled::plugin::TableFunction
{
public:

  CharacterSetsTool();
  ~CharacterSetsTool() {}

  class Generator : public drizzled::plugin::TableFunction::Generator 
  {
    CHARSET_INFO **cs;

  public:
    Generator(Field **arg);

    bool populate();

  };

  CharacterSetsTool::Generator *generator(Field **arg)
  {
    return new Generator(arg);
  }
};

#endif /* PLUGIN_DATA_ENGINE_CHARACTER_SETS_H */
