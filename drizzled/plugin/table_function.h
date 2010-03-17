/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Definitions required for TableFunction plugin
 *
 *  Copyright (C) 2010 Sun Microsystems
 *  Copyright (C) 2010 Monty Taylor
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

#ifndef DRIZZLED_PLUGIN_TABLE_FUNCTION_H
#define DRIZZLED_PLUGIN_TABLE_FUNCTION_H

#include <drizzled/definitions.h>
#include "drizzled/plugin.h"
#include "drizzled/plugin/plugin.h"
#include "drizzled/table_identifier.h"
#include "drizzled/message/table.pb.h"
#include "drizzled/charset.h"
#include "drizzled/field.h"

#include <string>
#include <set>
#include <algorithm>

namespace drizzled
{

extern int wild_case_compare(const CHARSET_INFO * const cs, 
                             const char *str,const char *wildstr);

namespace plugin
{

// Not thread safe, but plugins are just loaded in a single thread right
// now.
static const char *local_string_append(const char *arg1, const char *arg2)
{
  static char buffer[1024];
  char *buffer_ptr= buffer;
  strcpy(buffer_ptr, arg1);
  buffer_ptr+= strlen(arg1);
  buffer_ptr[0]= '-';
  buffer_ptr++;
  strcpy(buffer_ptr, arg2);

  return buffer;
}

class TableFunction : public Plugin
{
  TableFunction();
  TableFunction(const TableFunction &);
  TableFunction& operator=(const TableFunction &);

  message::Table proto;
  TableIdentifier identifier;
  std::string local_path;
  std::string local_schema;
  std::string local_table_name;
  std::string original_table_label;

  void setName(); // init name

  void init();

public:
  TableFunction(const char *schema_arg, const char *table_arg) :
    Plugin(local_string_append(schema_arg, table_arg) , "TableFunction"),
    identifier(schema_arg, table_arg),
    original_table_label(table_arg)
  {
    init();
  }

  virtual ~TableFunction() {}

  static bool addPlugin(TableFunction *function);
  static void removePlugin(TableFunction *) 
  { }
  static TableFunction *getFunction(const std::string &arg);
  static void getNames(const std::string &arg,
                       std::set<std::string> &set_of_names);

  enum ColumnType {
    BOOLEAN,
    NUMBER,
    STRING,
    VARBINARY
  };

  class Generator 
  {
    Field **columns;
    Field **columns_iterator;

  public:
    const CHARSET_INFO *scs;

    Generator(Field **arg);
    virtual ~Generator()
    { }

    /*
      Return type is bool meaning "are there more rows".
    */
    bool sub_populate(uint32_t field_size);

    virtual bool populate()
    {
      return false;
    }

    void push(uint64_t arg);
    void push(int64_t arg);
    void push(const char *arg, uint32_t length= 0);
    void push(const std::string& arg);
    void push(bool arg);
    void push();

    bool isWild(const std::string &predicate);
  };

  void define(message::Table &arg)
  { 
    arg.CopyFrom(proto);
  }

  const std::string &getTableLabel()
  { 
    return original_table_label;
  }

  const std::string &getIdentifierTableName()
  { 
    if (local_table_name.empty())
    {
      local_table_name= identifier.getTableName();
      std::transform(local_table_name.begin(), local_table_name.end(),
                     local_table_name.begin(), ::tolower);
    }

    return local_table_name;
  }

  const std::string &getSchemaHome()
  { 
    if (local_schema.empty())
    {
      local_schema= identifier.getSchemaName();
      std::transform(local_schema.begin(), local_schema.end(),
                     local_schema.begin(), ::tolower);
    }

    return local_schema;
  }

  const std::string &getPath()
  { 
    if (local_path.empty())
    {
      local_path= identifier.getPath();
      std::transform(local_path.begin(), local_path.end(),
                     local_path.begin(), ::tolower);
    }

    return local_path;
  }

  virtual Generator *generator(Field **arg);

  void add_field(const char *label,
                 message::Table::Field::FieldType type,
                 uint32_t length= 0);

  void add_field(const char *label,
                 uint32_t field_length= 64);

  void add_field(const char *label,
                 TableFunction::ColumnType type,
                 bool is_default_null= true);

  void add_field(const char *label,
                 TableFunction::ColumnType type,
                 uint32_t field_length,
                 bool is_default_null= false);
};

} /* namespace plugin */
} /* namespace drizzled */

#endif /* DRIZZLED_PLUGIN_TABLE_FUNCTION_H */
