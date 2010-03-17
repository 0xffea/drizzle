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

#ifndef DRIZZLED_STATEMENT_ALTER_TABLE_H
#define DRIZZLED_STATEMENT_ALTER_TABLE_H

#include <drizzled/statement/create_table.h>

namespace drizzled
{
class Session;
class TableList;

namespace message
{
  class Table;
}

namespace statement
{

class AlterTable : public CreateTable
{
public:
  AlterTable(Session *in_session) :
    CreateTable(in_session)
  { 
    create_table_message.set_update_timestamp(time(NULL));
  }

  bool execute();
};

} /* namespace statement */


bool alter_table(Session *session, char *new_db, char *new_name,
                 HA_CREATE_INFO *create_info,
                 message::Table &create_proto,
                 TableList *table_list,
                 AlterInfo *alter_info,
                 uint32_t order_num, order_st *order, bool ignore);

} /* namespace drizzled */
#endif /* DRIZZLED_STATEMENT_ALTER_TABLE_H */
