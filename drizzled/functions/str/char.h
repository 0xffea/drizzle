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

#ifndef DRIZZLED_FUNCTIONS_STR_CHAR_H
#define DRIZZLED_FUNCTIONS_STR_CHAR_H

#include <drizzled/functions/str/strfunc.h> 

class Item_func_char :public Item_str_func
{
public:
  Item_func_char(List<Item> &list) :Item_str_func(list)
  { collation.set(&my_charset_bin); }
  Item_func_char(List<Item> &list, const CHARSET_INFO * const cs) :Item_str_func(list)
  { collation.set(cs); }
  String *val_str(String *);
  void fix_length_and_dec()
  {
    max_length= arg_count * 4;
  }
  const char *func_name() const { return "char"; }
};

#endif /* DRIZZLED_FUNCTIONS_STR_CHAR_H */
