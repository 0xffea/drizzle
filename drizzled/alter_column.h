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

#ifndef DRIZZLED_ALTER_COLUMN_H
#define DRIZZLED_ALTER_COLUMN_H

#include <drizzled/sql_alloc.h>

class Item;
typedef struct st_mem_root MEM_ROOT;

class AlterColumn :public Sql_alloc {
public:
  const char *name;
  Item *def;
  AlterColumn(const char *par_name,Item *literal) :
    name(par_name),
    def(literal)
  {}
  
  /**
    Used to make a clone of this object for ALTER/CREATE TABLE
    @sa comment for Key_part_spec::clone
  */
  AlterColumn *clone(MEM_ROOT *mem_root) const
  {
    return new (mem_root) AlterColumn(*this);
  }
};

#endif /* DRIZZLED_ALTER_COLUMN_H */
