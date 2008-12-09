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

#include <drizzled/server_includes.h>
#include CSTDINT_H
#include <drizzled/item/decimal.h>

Item_decimal::Item_decimal(const char *str_arg, uint32_t length,
                           const CHARSET_INFO * const charset)
{
  str2my_decimal(E_DEC_FATAL_ERROR, str_arg, length, charset, &decimal_value);
  name= (char*) str_arg;
  decimals= (uint8_t) decimal_value.frac;
  fixed= 1;
  max_length= my_decimal_precision_to_length(decimal_value.intg + decimals,
                                             decimals, unsigned_flag);
}

Item_decimal::Item_decimal(int64_t val, bool unsig)
{
  int2my_decimal(E_DEC_FATAL_ERROR, val, unsig, &decimal_value);
  decimals= (uint8_t) decimal_value.frac;
  fixed= 1;
  max_length= my_decimal_precision_to_length(decimal_value.intg + decimals,
                                             decimals, unsigned_flag);
}


Item_decimal::Item_decimal(double val, int, int)
{
  double2my_decimal(E_DEC_FATAL_ERROR, val, &decimal_value);
  decimals= (uint8_t) decimal_value.frac;
  fixed= 1;
  max_length= my_decimal_precision_to_length(decimal_value.intg + decimals,
                                             decimals, unsigned_flag);
}

Item_decimal::Item_decimal(const char *str, const my_decimal *val_arg,
                           uint32_t decimal_par, uint32_t length)
{
  my_decimal2decimal(val_arg, &decimal_value);
  name= (char*) str;
  decimals= (uint8_t) decimal_par;
  max_length= length;
  fixed= 1;
}


Item_decimal::Item_decimal(my_decimal *value_par)
{
  my_decimal2decimal(value_par, &decimal_value);
  decimals= (uint8_t) decimal_value.frac;
  fixed= 1;
  max_length= my_decimal_precision_to_length(decimal_value.intg + decimals,
                                             decimals, unsigned_flag);
}


Item_decimal::Item_decimal(const unsigned char *bin, int precision, int scale)
{
  binary2my_decimal(E_DEC_FATAL_ERROR, bin,
                    &decimal_value, precision, scale);
  decimals= (uint8_t) decimal_value.frac;
  fixed= 1;
  max_length= my_decimal_precision_to_length(precision, decimals,
                                             unsigned_flag);
}

int64_t Item_decimal::val_int()
{
  int64_t result;
  my_decimal2int(E_DEC_FATAL_ERROR, &decimal_value, unsigned_flag, &result);
  return result;
}

double Item_decimal::val_real()
{
  double result;
  my_decimal2double(E_DEC_FATAL_ERROR, &decimal_value, &result);
  return result;
}

String *Item_decimal::val_str(String *result)
{
  result->set_charset(&my_charset_bin);
  my_decimal2string(E_DEC_FATAL_ERROR, &decimal_value, 0, 0, 0, result);
  return result;
}

void Item_decimal::print(String *str, enum_query_type)
{
  my_decimal2string(E_DEC_FATAL_ERROR, &decimal_value, 0, 0, 0, &str_value);
  str->append(str_value);
}

bool Item_decimal::eq(const Item *item, bool) const
{
  if (type() == item->type() && item->basic_const_item())
  {
    /*
      We need to cast off const to call val_decimal(). This should
      be OK for a basic constant. Additionally, we can pass 0 as
      a true decimal constant will return its internal decimal
      storage and ignore the argument.
    */
    Item *arg= (Item*) item;
    my_decimal *value= arg->val_decimal(0);
    return !my_decimal_cmp(&decimal_value, value);
  }
  return 0;
}


void Item_decimal::set_decimal_value(my_decimal *value_par)
{
  my_decimal2decimal(value_par, &decimal_value);
  decimals= (uint8_t) decimal_value.frac;
  unsigned_flag= !decimal_value.sign();
  max_length= my_decimal_precision_to_length(decimal_value.intg + decimals,
                                             decimals, unsigned_flag);
}

int Item_decimal::save_in_field(Field *field, bool)
{
  field->set_notnull();
  return field->store_decimal(&decimal_value);
}

