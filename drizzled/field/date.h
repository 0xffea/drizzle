/* - mode: c++ c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 MySQL
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

#ifndef DRIZZLED_FIELD_DATE_H
#define DRIZZLED_FIELD_DATE_H

#include <drizzled/field/str.h>

namespace drizzled
{

class Field_date :public Field_str {
public:

  using Field::store;
  using Field::val_int;
  using Field::val_str;
  using Field::cmp;

  Field_date(unsigned char *ptr_arg,
             unsigned char *null_ptr_arg,
             unsigned char null_bit_arg,
             const char *field_name_arg,
             const CHARSET_INFO * const cs)
    :Field_str(ptr_arg,
               10,
               null_ptr_arg,
               null_bit_arg,
               field_name_arg,
               cs)
  {}
  Field_date(bool maybe_null_arg,
             const char *field_name_arg,
             const CHARSET_INFO * const cs)
    :Field_str((unsigned char*) 0,
               10,
               maybe_null_arg ? (unsigned char*) "": 0,
               0,
               field_name_arg,
               cs) 
  {}
  enum_field_types type() const { return DRIZZLE_TYPE_DATE;}
  enum_field_types real_type() const { return DRIZZLE_TYPE_DATE; }
  enum ha_base_keytype key_type() const { return HA_KEYTYPE_UINT24; }
  enum Item_result cmp_type () const { return INT_RESULT; }
  int  store(const char *to,uint32_t length,
             const CHARSET_INFO * const charset);
  int  store(double nr);
  int  store(int64_t nr, bool unsigned_val);
  int store_time(DRIZZLE_TIME *ltime, enum enum_drizzle_timestamp_type type);
  int reset(void) { ptr[0]=ptr[1]=ptr[2]=0; return 0; }
  double val_real(void);
  int64_t val_int(void);
  String *val_str(String*,String *);
  int cmp(const unsigned char *,const unsigned char *);
  void sort_string(unsigned char *buff,uint32_t length);
  uint32_t pack_length() const { return 3; }
  void sql_type(String &str) const;
  bool can_be_compared_as_int64_t() const { return true; }
  bool zero_pack() const { return 1; }
  bool get_date(DRIZZLE_TIME *ltime,uint32_t fuzzydate);
  bool get_time(DRIZZLE_TIME *ltime);
};

} /* namespace drizzled */

#endif /* DRIZZLED_FIELD_DATE_H */
