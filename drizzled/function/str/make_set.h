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

#ifndef DRIZZLED_FUNCTION_STR_MAKE_SET_H
#define DRIZZLED_FUNCTION_STR_MAKE_SET_H

#include <drizzled/function/str/strfunc.h>

namespace drizzled
{

class Item_func_make_set :public Item_str_func
{
  Item *item;
  String tmp_str;

public:
  using Item::split_sum_func;
  Item_func_make_set(Item *a,List<Item> &list) :Item_str_func(list),item(a) {}
  String *val_str(String *str);
  bool fix_fields(Session *session, Item **ref)
  {
    assert(fixed == 0);
    return ((!item->fixed && item->fix_fields(session, &item)) ||
            item->check_cols(1) ||
            Item_func::fix_fields(session, ref));
  }
  void split_sum_func(Session *session, Item **ref_pointer_array, List<Item> &fields);
  void fix_length_and_dec();
  void update_used_tables();
  const char *func_name() const { return "make_set"; }

  bool walk(Item_processor processor, bool walk_subquery, unsigned char *arg)
  {
    return item->walk(processor, walk_subquery, arg) ||
      Item_str_func::walk(processor, walk_subquery, arg);
  }
  Item *transform(Item_transformer transformer, unsigned char *arg);
  virtual void print(String *str, enum_query_type query_type);
};

} /* namespace drizzled */

#endif /* DRIZZLED_FUNCTION_STR_MAKE_SET_H */
