/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
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

/* 
  This is a "work in progress". The concept needs to be replicated throughout
  the code, but we will start with baby steps for the moment. To not incur
  cost until we are complete, for the moment it will do no allocation.

  This is mainly here so that it can be used in the SE interface for
  the time being.

  This will replace Table_ident.
  */

#ifndef DRIZZLED_TABLE_IDENTIFIER_H
#define DRIZZLED_TABLE_IDENTIFIER_H

#include <drizzled/enum.h>
#include "drizzled/definitions.h"
#include "drizzled/message/table.pb.h"

#include "drizzled/schema_identifier.h"

#include <string.h>

#include <assert.h>

#include <ostream>
#include <set>
#include <algorithm>
#include <functional>

namespace drizzled {

uint32_t filename_to_tablename(const char *from, char *to, uint32_t to_length);
size_t build_table_filename(std::string &buff, const char *db, const char *table_name, bool is_tmp);

class TableIdentifier : public SchemaIdentifier
{
public:
  typedef message::Table::TableType Type;
private:

  Type type;
  std::string path;
  std::string table_name;
  std::string lower_table_name;
  std::string sql_path;

  void init();

public:
  TableIdentifier( const SchemaIdentifier &schema,
                   const std::string &table_name_arg,
                   Type tmp_arg= message::Table::STANDARD) :
    SchemaIdentifier(schema),
    type(tmp_arg),
    table_name(table_name_arg)
  { 
    init();
  }

  TableIdentifier( const std::string &db_arg,
                   const std::string &table_name_arg,
                   Type tmp_arg= message::Table::STANDARD) :
    SchemaIdentifier(db_arg),
    type(tmp_arg),
    table_name(table_name_arg)
  { 
    init();
  }

  TableIdentifier( const std::string &schema_name_arg,
                   const std::string &table_name_arg,
                   const std::string &path_arg ) :
    SchemaIdentifier(schema_name_arg),
    type(message::Table::TEMPORARY),
    path(path_arg),
    table_name(table_name_arg)
  { 
    init();
  }

  bool compare(std::string schema_arg, std::string table_arg);

  bool isTmp() const
  {
    if (type == message::Table::TEMPORARY || type == message::Table::INTERNAL)
      return true;
    return false;
  }

  Type getType() const
  {
    return type;
  }

  const std::string &getSQLPath();

  const std::string &getPath();

  void setPath(const std::string &new_path)
  {
    path= new_path;
  }

  const std::string getTableName() const
  {
    return table_name;
  }

  void copyToTableMessage(message::Table &message);

  friend std::ostream& operator<<(std::ostream& output, const TableIdentifier &identifier)
  {
    const char *type_str;

    output << "TableIdentifier:(";
    output <<  identifier.getSchemaName();
    output << ", ";
    output << identifier.getTableName();
    output << ", ";

    switch (identifier.type) {
    case message::Table::STANDARD:
      type_str= "standard";
      break;
    case message::Table::INTERNAL:
      type_str= "internal";
      break;
    case message::Table::TEMPORARY:
      type_str= "temporary";
      break;
    case message::Table::FUNCTION:
      type_str= "function";
      break;
    }

    output << type_str;
    output << ", ";
    output << identifier.path;
    output << ")";

    return output;  // for multiple << operators.
  }

  friend bool operator==(TableIdentifier &left, TableIdentifier &right)
  {
    if (left.type == right.type)
    {
      if (static_cast<SchemaIdentifier &>(left) == static_cast<SchemaIdentifier &>(right))
      {
        if (left.lower_table_name == right.lower_table_name)
        {
          return true;
        }
      }
    }

    return false;
  }

};

typedef std::vector <TableIdentifier> TableIdentifierList;

} /* namespace drizzled */

#endif /* DRIZZLED_TABLE_IDENTIFIER_H */
