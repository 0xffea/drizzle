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


#ifndef DRIZZLED_SELECT_DUMPVAR_H
#define DRIZZLED_SELECT_DUMPVAR_H

#include "drizzled/error.h"
#include "drizzled/error.h"

#include <vector>

class select_dumpvar :public select_result_interceptor {
  ha_rows row_count;
public:
  std::vector<my_var *> var_list;
  select_dumpvar()  { var_list.clear(); row_count= 0;}
  ~select_dumpvar() {}

  int prepare(List<Item> &list, Select_Lex_Unit *u)
  {
    unit= u;

    if (var_list.size() != list.elements)
    {
      my_message(ER_WRONG_NUMBER_OF_COLUMNS_IN_SELECT,
                 ER(ER_WRONG_NUMBER_OF_COLUMNS_IN_SELECT), MYF(0));
      return 1;
    }
    return 0;
  }

  void cleanup()
  {
    row_count= 0;
  }


  bool send_data(List<Item> &items)
  {
    
    std::vector<my_var *>::const_iterator iter= var_list.begin();

    List_iterator<Item> it(items);
    Item *item;
    my_var *current_var;

    if (unit->offset_limit_cnt)
    {						// using limit offset,count
      unit->offset_limit_cnt--;
      return(0);
    }
    if (row_count++)
    {
      my_message(ER_TOO_MANY_ROWS, ER(ER_TOO_MANY_ROWS), MYF(0));
      return(1);
    }
    while ((iter != var_list.end()) && (item= it++))
    {
      current_var= *iter;
      if (current_var->local == 0)
      {
        Item_func_set_user_var *suv= new Item_func_set_user_var(current_var->s, item);
        suv->fix_fields(session, 0);
        suv->check(0);
        suv->update();
      }
      ++iter;
    }
    return(session->is_error());
  }

  bool send_eof()
  {
    if (! row_count)
      push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                   ER_SP_FETCH_NO_DATA, ER(ER_SP_FETCH_NO_DATA));
    /*
      In order to remember the value of affected rows for ROW_COUNT()
      function, SELECT INTO has to have an own SQLCOM.
TODO: split from SQLCOM_SELECT
  */
    session->my_ok(row_count);
    return 0;
  }

};

#endif /* DRIZZLED_SELECT_DUMPVAR_H */
