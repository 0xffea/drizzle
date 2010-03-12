/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
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

#ifndef DRIZZLED_PLUGIN_MODULE_H
#define DRIZZLED_PLUGIN_MODULE_H

#include "drizzled/lex_string.h"
#include "drizzled/plugin/manifest.h"

namespace drizzled
{
class sys_var;

namespace plugin
{

class Library;

/* A plugin module */
class Module
{
  const std::string name;
  const Manifest *manifest;
public:
  Library *plugin_dl;
  bool isInited;
  sys_var *system_vars;         /* server variables for this plugin */
  Module(const Manifest *manifest_arg, Library *library_arg)
    : name(manifest_arg->name), manifest(manifest_arg), plugin_dl(library_arg),
      isInited(false),
      system_vars(NULL) {}
      
  Module(const Manifest *manifest_arg)
    : name(manifest_arg->name), manifest(manifest_arg), plugin_dl(NULL),
      isInited(false),
      system_vars(NULL) {}
      
  const std::string& getName() const
  {
    return name;
  }

  const Manifest& getManifest() const
  {
    return *manifest;
  }
};

} /* namespace plugin */
} /* namespace drizzled */

#endif /* DRIZZLED_PLUGIN_MODULE_H */
