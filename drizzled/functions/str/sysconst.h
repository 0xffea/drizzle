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

#ifndef DRIZZLED_FUNCTIONS_STR_SYSCONST_H
#define DRIZZLED_FUNCTIONS_STR_SYSCONST_H

#include <drizzled/functions/str/strfunc.h> 

class Item_func_sysconst :public Item_str_func
{
public:
  Item_func_sysconst()
  { collation.set(system_charset_info,DERIVATION_SYSCONST); }
  Item *safe_charset_converter(const CHARSET_INFO * const tocs);
  /*
    Used to create correct Item name in new converted item in
    safe_charset_converter, return string representation of this function
    call
  */
  virtual const char *fully_qualified_func_name() const = 0;
  bool check_vcol_func_processor(unsigned char *int_arg __attribute__((unused)))
  { return true; }
};

#endif /* DRIZZLED_FUNCTIONS_STR_SYSCONST_H */
