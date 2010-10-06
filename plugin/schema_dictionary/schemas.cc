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
#include "plugin/schema_dictionary/dictionary.h"

using namespace std;
using namespace drizzled;

SchemasTool::SchemasTool() :
  plugin::TableFunction("DATA_DICTIONARY", "SCHEMAS")
{
  add_field("SCHEMA_NAME");
  add_field("DEFAULT_COLLATION_NAME");
  add_field("SCHEMA_CREATION_TIME");
  add_field("SCHEMA_UPDATE_TIME");
  add_field("SCHEMA_UUID", plugin::TableFunction::STRING, 36, true);
  add_field("SCHEMA_VERSION", plugin::TableFunction::NUMBER, 0, true);
}

SchemasTool::Generator::Generator(Field **arg) :
  plugin::TableFunction::Generator(arg),
  schema_generator(getSession())
{
}
  
bool SchemasTool::Generator::nextSchema()
{
  const drizzled::message::Schema *schema_ptr;
  while ((schema_ptr= schema_generator))
  {
    schema.CopyFrom(*schema_ptr);
    return true;
  }

  return false;
}


bool SchemasTool::Generator::populate()
{
  if (nextSchema())
  {
    fill();
    return true;
  }

  return false;
}

/**
  A lack of a parsed schema file means we are using defaults.
*/
void SchemasTool::Generator::fill()
{
  /* SCHEMA_NAME */
  push(schema.name());

  /* DEFAULT_COLLATION_NAME */
  push(schema.collation());

  /* SCHEMA_CREATION_TIME */
  time_t time_arg= schema.creation_timestamp();
  char buffer[40];
  struct tm tm_buffer;

  localtime_r(&time_arg, &tm_buffer);
  strftime(buffer, sizeof(buffer), "%a %b %d %H:%M:%S %Y", &tm_buffer);
  push(buffer);

  /* SCHEMA_UPDATE_TIME */
  time_arg= schema.update_timestamp();
  localtime_r(&time_arg, &tm_buffer);
  strftime(buffer, sizeof(buffer), "%a %b %d %H:%M:%S %Y", &tm_buffer);
  push(buffer);

  /* SCHEMA_UUID */
  push(schema.uuid());

  /* SCHEMA_VERSION */
  push(schema.version());
}
