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

#ifndef DRIZZLED_PLUGIN_REGISTRY_H
#define DRIZZLED_PLUGIN_REGISTRY_H

#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include "drizzled/gettext.h"
#include "drizzled/unireg.h"
#include "drizzled/errmsg_print.h"

namespace drizzled
{
namespace plugin
{
class Module;
class Plugin;

class Registry
{
private:
  std::map<std::string, Module *> module_map;
  std::map<std::string, const Plugin *> plugin_registry;

  Module *current_module;

  Registry()
   : module_map(),
     plugin_registry(),
     current_module(NULL)
  { }

  Registry(const Registry&);
  Registry& operator=(const Registry&);
public:

  static plugin::Registry& singleton()
  {
    static plugin::Registry *registry= new plugin::Registry();
    return *registry;
  }

  static void shutdown()
  {
    plugin::Registry& registry= singleton();
    delete &registry;
  }

  Module *find(const LEX_STRING *name);

  void add(Module *module);

  void setCurrentModule(Module *module)
  {
    current_module= module;
  }

  void clearCurrentModule()
  {
    current_module= NULL;
  }

  std::vector<Module *> getList(bool active);

  const std::map<std::string, const Plugin *> &getPluginsMap() const
  {
    return plugin_registry;
  }

  template<class T>
  void add(T *plugin)
  {
    plugin->setModule(current_module);
    bool failed= false;
    std::string plugin_name(plugin->getName());
    std::transform(plugin_name.begin(), plugin_name.end(),
                   plugin_name.begin(), ::tolower);
    if (plugin_registry.find(plugin_name) != plugin_registry.end())
    {
      errmsg_printf(ERRMSG_LVL_ERROR,
                    _("Loading plugin %s failed: a plugin by that name already "
                      "exists."), plugin->getName().c_str());
      failed= true;
    }
    if (T::addPlugin(plugin))
      failed= true;
    if (failed)
    {
      errmsg_printf(ERRMSG_LVL_ERROR,
                    _("Fatal error: Failed initializing %s plugin."),
                    plugin->getName().c_str());
      unireg_abort(1);
    }
  }

  template<class T>
  void remove(T *plugin)
  {
    std::string plugin_name(plugin->getName());
    std::transform(plugin_name.begin(), plugin_name.end(),
                   plugin_name.begin(), ::tolower);
    T::removePlugin(plugin);
    plugin_registry.erase(plugin_name);
  }

};

} /* end namespace plugin */
} /* end namespace drizzled */
#endif /* DRIZZLED_PLUGIN_REGISTRY_H */
