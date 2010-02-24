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

#include "config.h"

#include <plugin/function_engine/function.h>
#include <plugin/function_engine/cursor.h>

#include <string>

using namespace std;
using namespace drizzled;

Function::Function(const std::string &name_arg) :
  drizzled::plugin::StorageEngine(name_arg,
                                  HTON_ALTER_NOT_SUPPORTED |
                                  HTON_HAS_DATA_DICTIONARY |
                                  HTON_SKIP_STORE_LOCK |
                                  HTON_TEMPORARY_NOT_SUPPORTED)
{
}


Cursor *Function::create(TableShare &table, memory::Root *mem_root)
{
  return new (mem_root) FunctionCursor(*this, table);
}

int Function::doGetTableDefinition(Session &,
                                     const char *path,
                                     const char *,
                                     const char *,
                                     const bool,
                                     message::Table *table_proto)
{
  string tab_name(path);
  transform(tab_name.begin(), tab_name.end(),
            tab_name.begin(), ::tolower);

  drizzled::plugin::TableFunction *function= getFunction(tab_name);

  if (not function)
  {
    return ENOENT;
  }

  if (table_proto)
  {
    function->define(*table_proto);
  }

  return EEXIST;
}


void Function::doGetTableNames(drizzled::CachedDirectory&, 
                               string &db, 
                               set<string> &set_of_names)
{
  drizzled::plugin::TableFunction::getNames(db, set_of_names);
}

void Function::doGetSchemaNames(std::set<std::string>& set_of_names)
{
  set_of_names.insert("information_schema"); // special cases suck
}


static drizzled::plugin::StorageEngine *function_plugin= NULL;

static int init(drizzled::plugin::Registry &registry)
{
  function_plugin= new(std::nothrow) Function("FunctionEngine");

  if (not function_plugin)
  {
    return 1;
  }

  registry.add(function_plugin);

  return 0;
}

static int finalize(drizzled::plugin::Registry &registry)
{
  registry.remove(function_plugin);
  delete function_plugin;

  return 0;
}

DRIZZLE_DECLARE_PLUGIN
{
  DRIZZLE_VERSION_ID,
  "FunctionEngine",
  "1.0",
  "Brian Aker",
  "Function Engine provides the infrastructure for Table Functions,etc.",
  PLUGIN_LICENSE_GPL,
  init,     /* Plugin Init */
  finalize,     /* Plugin Deinit */
  NULL,               /* system variables */
  NULL                /* config options   */
}
DRIZZLE_DECLARE_PLUGIN_END;
