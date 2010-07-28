/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Brian Aker
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

#ifndef DRIZZLED_IDENTIFIER_TABLE_H
#define DRIZZLED_IDENTIFIER_TABLE_H

#include <drizzled/enum.h>
#include "drizzled/definitions.h"
#include "drizzled/message/table.pb.h"

#include "drizzled/identifier.h"

#include <string.h>

#include <assert.h>

#include <ostream>
#include <set>
#include <algorithm>
#include <functional>

#include <boost/functional/hash.hpp>

namespace drizzled {

class Table;

class TableIdentifier : public SchemaIdentifier
{
public:
  typedef message::Table::TableType Type;
  typedef std::vector<char> Key;
private:

  Type type;
  std::string path;
  std::string table_name;
  std::string lower_table_name;
  std::string sql_path;
  Key key;
  size_t hash_value;

  void init();

  size_t getKeySize() const
  {
    return getSchemaName().size() + getTableName().size() + 2;
  }

public:
  TableIdentifier(const Table &table);
                   
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

  using SchemaIdentifier::compare;
  bool compare(std::string schema_arg, std::string table_arg) const;

  bool isTmp() const
  {
    if (type == message::Table::TEMPORARY || type == message::Table::INTERNAL)
      return true;
    return false;
  }

  bool isView() const // Not a SQL view, but a view for I_S
  {
    if (type == message::Table::FUNCTION)
      return true;
    return false;
  }

  Type getType() const
  {
    return type;
  }

  const std::string &getSQLPath();

  const std::string &getPath() const;

  void setPath(const std::string &new_path)
  {
    path= new_path;
  }

  const std::string &getTableName() const
  {
    return table_name;
  }

  void copyToTableMessage(message::Table &message) const;

  friend bool operator<(const TableIdentifier &left, const TableIdentifier &right)
  {
    if (left.getKey() < right.getKey())
    {
      return true;
    }

    return false;
  }

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
    output << ", ";
    output << identifier.getHashValue();
    output << ")";

    return output;  // for multiple << operators.
  }

  friend bool operator==(TableIdentifier &left, TableIdentifier &right)
  {
    if (left.getKey() == right.getKey())
      return true;

    return false;
  }

  static uint32_t filename_to_tablename(const char *from, char *to, uint32_t to_length);
  static size_t build_table_filename(std::string &buff, const char *db, const char *table_name, bool is_tmp);
  static size_t build_tmptable_filename(std::string &buffer);
  static size_t build_tmptable_filename(std::vector<char> &buffer);

  /*
    Create a table cache key

    SYNOPSIS
    createKey()
    key			Create key here (must be of size MAX_DBKEY_LENGTH)
    table_list		Table definition

    IMPLEMENTATION
    The table cache_key is created from:
    db_name + \0
    table_name + \0

    if the table is a tmp table, we add the following to make each tmp table
    unique on the slave:

    4 bytes for master thread id
    4 bytes pseudo thread id

    RETURN
    Length of key
  */
  static uint32_t createKey(char *key, const char *db_arg, const char *table_name_arg)
  {
    uint32_t key_length;
    char *key_pos= key;

    key_pos= strcpy(key_pos, db_arg) + strlen(db_arg);
    key_pos= strcpy(key_pos+1, table_name_arg) +
      strlen(table_name_arg);
    key_length= (uint32_t)(key_pos-key)+1;

    return key_length;
  }

  static uint32_t createKey(char *key, const TableIdentifier &identifier)
  {
    uint32_t key_length;
    char *key_pos= key;

    key_pos= strcpy(key_pos, identifier.getSchemaName().c_str()) + identifier.getSchemaName().length();
    key_pos= strcpy(key_pos + 1, identifier.getTableName().c_str()) + identifier.getTableName().length();
    key_length= (uint32_t)(key_pos-key)+1;

    return key_length;
  }

  size_t getHashValue() const
  {
    return hash_value;
  }

  const Key &getKey() const
  {
    return key;
  }
};

std::size_t hash_value(TableIdentifier const& b);

typedef std::vector <TableIdentifier> TableIdentifiers;

} /* namespace drizzled */

#endif /* DRIZZLED_IDENTIFIER_TABLE_H */
