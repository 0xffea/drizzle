/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
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

#include <string>
#include <vector>
#include <map>

#include "drizzled/plugin/registry.h"

#include "drizzled/plugin.h"
#include "drizzled/plugin/library.h"
#include "drizzled/show.h"
#include "drizzled/cursor.h"

using namespace std;

namespace drizzled
{

plugin::Registry::~Registry()
{
  map<string, plugin::Plugin *>::iterator plugin_iter= plugin_registry.begin();
  while (plugin_iter != plugin_registry.end())
  {
    delete (*plugin_iter).second;
    ++plugin_iter;
  }
  plugin_registry.clear();

  map<string, plugin::Module *>::iterator module_iter= module_map.begin();
  while (module_iter != module_map.end())
  {
    delete (*module_iter).second;
    ++module_iter;
  }
  module_map.clear();

  map<string, plugin::Library *>::iterator library_iter= library_map.begin();
  while (library_iter != library_map.end())
  {
    delete (*library_iter).second;
    ++library_iter;
  }
  library_map.clear();
}

void plugin::Registry::shutdown()
{
  plugin::Registry& registry= singleton();
  delete &registry;
}

plugin::Module *plugin::Registry::find(string name)
{
  transform(name.begin(), name.end(), name.begin(), ::tolower);

  map<string, plugin::Module *>::iterator map_iter;
  map_iter= module_map.find(name);
  if (map_iter != module_map.end())
    return (*map_iter).second;
  return(0);
}

void plugin::Registry::add(plugin::Module *handle)
{
  string add_str(handle->getName());
  transform(add_str.begin(), add_str.end(),
            add_str.begin(), ::tolower);

  module_map[add_str]= handle;
}


vector<plugin::Module *> plugin::Registry::getList(bool active)
{
  plugin::Module *plugin= NULL;

  vector<plugin::Module *> plugins;
  plugins.reserve(module_map.size());

  map<string, plugin::Module *>::iterator map_iter;
  for (map_iter= module_map.begin();
       map_iter != module_map.end();
       map_iter++)
  {
    plugin= (*map_iter).second;
    if (active)
      plugins.push_back(plugin);
    else if (plugin->isInited)
      plugins.push_back(plugin);
  }

  return plugins;
}

plugin::Library *plugin::Registry::addLibrary(const string &plugin_name)
{

  /* If this dll is already loaded just return it */
  plugin::Library *library= findLibrary(plugin_name);
  if (library != NULL)
  {
    return library;
  }

  library= plugin::Library::loadLibrary(plugin_name);
  if (library != NULL)
  {
    /* Add this dll to the map */
    library_map.insert(make_pair(plugin_name, library));
  }

  return library;
}

void plugin::Registry::removeLibrary(const string &plugin_name)
{
  map<string, plugin::Library *>::iterator iter=
    library_map.find(plugin_name);
  if (iter != library_map.end())
  {
    library_map.erase(iter);
    delete (*iter).second;
  }
}

plugin::Library *plugin::Registry::findLibrary(const string &plugin_name) const
{
  map<string, plugin::Library *>::const_iterator iter=
    library_map.find(plugin_name);
  if (iter != library_map.end())
    return (*iter).second;
  return NULL;
}

} /* namespace drizzled */
