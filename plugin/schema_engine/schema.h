/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Brian Aker
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

#ifndef PLUGIN_SCHEMA_ENGINE_SCHEMA_H
#define PLUGIN_SCHEMA_ENGINE_SCHEMA_H

#include <assert.h>
#include <drizzled/plugin/storage_engine.h>
#include <drizzled/data_home.h>

extern const drizzled::CHARSET_INFO *default_charset_info;

static const char *schema_exts[] = {
  NULL
};

class Schema : public drizzled::plugin::StorageEngine
{
  int write_schema_file(const char *path, const drizzled::message::Schema &db);

public:
  Schema();

  ~Schema()
  { }

  int doCreateTable(drizzled::Session *,
                    const char *,
                    drizzled::Table&,
                    drizzled::message::Table&)
  {
    return EPERM;
  }

  drizzled::Cursor *create(drizzled::TableShare &,
                           drizzled::memory::Root *)
  {
    return NULL;
  }

  int doDropTable(drizzled::Session&, const std::string) 
  { 
    return EPERM; 
  }

  void doGetSchemaNames(std::set<std::string>& set_of_names);
  bool doGetSchemaDefinition(const std::string &schema_name, drizzled::message::Schema &proto);

  bool doCreateSchema(const drizzled::message::Schema &schema_message);

  bool doAlterSchema(const drizzled::message::Schema &schema_message);

  bool doDropSchema(const std::string &schema_name);

  const char **bas_ext() const 
  {
    return schema_exts;
  }
};

#endif /* PLUGIN_SCHEMA_ENGINE_SCHEMA_H */