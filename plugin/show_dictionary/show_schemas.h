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

#ifndef PLUGIN_SHOW_DICTIONARY_SHOW_SCHEMAS_H
#define PLUGIN_SHOW_DICTIONARY_SHOW_SCHEMAS_H

class ShowSchemas : public drizzled::plugin::TableFunction
{
public:

  ShowSchemas();

  ShowSchemas(const char *schema_arg, const char *table_arg) :
    drizzled::plugin::TableFunction(schema_arg, table_arg)
  { }

  class Generator : public drizzled::plugin::TableFunction::Generator 
  {
    drizzled::SchemaIdentifiers schema_names;
    drizzled::SchemaIdentifiers::const_iterator schema_iterator;

    bool is_schema_primed;

    virtual void fill();
    virtual bool checkSchema();

  public:
    Generator(drizzled::Field **arg);

    bool populate();
    bool nextSchemaCore();
    bool nextSchema();
    bool isSchemaPrimed()
    {
      return is_schema_primed;
    }
  };

  Generator *generator(drizzled::Field **arg)
  {
    return new Generator(arg);
  }
};


#endif /* PLUGIN_SHOW_DICTIONARY_SHOW_SCHEMAS_H */
