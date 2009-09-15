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

/**
 * @file Declaration of the AlterInfo class
 */

#ifndef DRIZZLED_ALTER_INFO_H
#define DRIZZLED_ALTER_INFO_H

#include "drizzled/base.h"
#include "drizzled/enum.h"
#include "drizzled/sql_list.h" /** @TODO use STL vectors! */

#include <bitset>

/* Some forward declarations needed */
class CreateField;
class Alter_drop;
class Alter_column;
class Key;

enum enum_alter_info_flags
{
  ALTER_ADD_COLUMN= 0,
  ALTER_DROP_COLUMN,
  ALTER_CHANGE_COLUMN,
  ALTER_COLUMN_STORAGE,
  ALTER_COLUMN_FORMAT,
  ALTER_COLUMN_ORDER,
  ALTER_ADD_INDEX,
  ALTER_DROP_INDEX,
  ALTER_RENAME,
  ALTER_ORDER,
  ALTER_OPTIONS,
  ALTER_COLUMN_DEFAULT,
  ALTER_KEYS_ONOFF,
  ALTER_STORAGE,
  ALTER_ROW_FORMAT,
  ALTER_CONVERT,
  ALTER_FORCE,
  ALTER_RECREATE,
  ALTER_TABLE_REORG,
  ALTER_FOREIGN_KEY
};

enum tablespace_op_type
{
  NO_TABLESPACE_OP,
  DISCARD_TABLESPACE,
  IMPORT_TABLESPACE
};

/**
 * Contains information about the parsed CREATE or ALTER TABLE statement.
 *
 * This structure contains a list of columns or indexes to be created,
 * altered or dropped.
 */
class AlterInfo
{
public:
  List<Alter_drop> drop_list;
  List<Alter_column> alter_list;
  List<Key> key_list;
  List<CreateField> create_list;
  std::bitset<32> flags;
  enum enum_enable_or_disable keys_onoff;
  enum tablespace_op_type tablespace_op;
  uint32_t no_parts;
  enum ha_build_method build_method;
  CreateField *datetime_field;
  bool error_if_not_empty;

  AlterInfo();
  AlterInfo(const AlterInfo &rhs, MEM_ROOT *mem_root);
  void reset();
private:
  AlterInfo &operator=(const AlterInfo &rhs); // not implemented
  AlterInfo(const AlterInfo &rhs);            // not implemented
};

#endif /* DRIZZLED_ALTER_INFO_H */
