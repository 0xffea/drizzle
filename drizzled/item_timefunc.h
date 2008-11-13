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

#ifndef DRIZZLED_ITEM_TIMEFUNC_H
#define DRIZZLED_ITEM_TIMEFUNC_H

#include <drizzled/functions/time/weekday.h>
#include <drizzled/functions/time/str_timefunc.h>
#include <drizzled/functions/time/date.h>
#include <drizzled/functions/time/curdate.h>
#include <drizzled/functions/time/curtime.h>
#include <drizzled/functions/time/dayname.h>
#include <drizzled/functions/time/dayofmonth.h>
#include <drizzled/functions/time/dayofyear.h>
#include <drizzled/functions/time/from_days.h>
#include <drizzled/functions/time/hour.h>
#include <drizzled/functions/time/minute.h>
#include <drizzled/functions/time/month.h>
#include <drizzled/functions/time/now.h>
#include <drizzled/functions/time/quarter.h>
#include <drizzled/functions/time/period_add.h>
#include <drizzled/functions/time/period_diff.h>
#include <drizzled/functions/time/second.h>
#include <drizzled/functions/time/sysdate_local.h>
#include <drizzled/functions/time/time_to_sec.h>
#include <drizzled/functions/time/to_days.h>
#include <drizzled/functions/time/unix_timestamp.h>
#include <drizzled/functions/time/week.h>
#include <drizzled/functions/time/week_mode.h>
#include <drizzled/functions/time/year.h>
#include <drizzled/functions/time/yearweek.h>

/* Function items used by mysql */

enum date_time_format_types 
{ 
  TIME_ONLY= 0, TIME_MICROSECOND, DATE_ONLY, DATE_TIME, DATE_TIME_MICROSECOND
};

bool get_interval_value(Item *args,interval_type int_type,
			       String *str_value, INTERVAL *interval);

class Item_func_date_format :public Item_str_func
{
  int fixed_length;
  const bool is_time_format;
  String value;
public:
  Item_func_date_format(Item *a,Item *b,bool is_time_format_arg)
    :Item_str_func(a,b),is_time_format(is_time_format_arg) {}
  String *val_str(String *str);
  const char *func_name() const
    { return is_time_format ? "time_format" : "date_format"; }
  void fix_length_and_dec();
  uint32_t format_length(const String *format);
  bool eq(const Item *item, bool binary_cmp) const;
};


class Item_func_from_unixtime :public Item_date_func
{
  Session *session;
 public:
  Item_func_from_unixtime(Item *a) :Item_date_func(a) {}
  int64_t val_int();
  String *val_str(String *str);
  const char *func_name() const { return "from_unixtime"; }
  void fix_length_and_dec();
  bool get_date(DRIZZLE_TIME *res, uint32_t fuzzy_date);
};


/* 
  We need Time_zone class declaration for storing pointers in
  Item_func_convert_tz.
*/
class Time_zone;

/*
  This class represents CONVERT_TZ() function.
  The important fact about this function that it is handled in special way.
  When such function is met in expression time_zone system tables are added
  to global list of tables to open, so later those already opened and locked
  tables can be used during this function calculation for loading time zone
  descriptions.
*/
class Item_func_convert_tz :public Item_date_func
{
  /*
    If time zone parameters are constants we are caching objects that
    represent them (we use separate from_tz_cached/to_tz_cached members
    to indicate this fact, since NULL is legal value for from_tz/to_tz
    members.
  */
  bool from_tz_cached, to_tz_cached;
  Time_zone *from_tz, *to_tz;
 public:
  Item_func_convert_tz(Item *a, Item *b, Item *c):
    Item_date_func(a, b, c), from_tz_cached(0), to_tz_cached(0) {}
  int64_t val_int();
  String *val_str(String *str);
  const char *func_name() const { return "convert_tz"; }
  void fix_length_and_dec();
  bool get_date(DRIZZLE_TIME *res, uint32_t fuzzy_date);
  void cleanup();
};


class Item_func_sec_to_time :public Item_str_timefunc
{
public:
  Item_func_sec_to_time(Item *item) :Item_str_timefunc(item) {}
  double val_real()
  {
    assert(fixed == 1);
    return (double) Item_func_sec_to_time::val_int();
  }
  int64_t val_int();
  String *val_str(String *);
  void fix_length_and_dec()
  { 
    Item_str_timefunc::fix_length_and_dec();
    collation.set(&my_charset_bin);
    maybe_null=1;
  }
  const char *func_name() const { return "sec_to_time"; }
  bool result_as_int64_t() { return true; }
};


class Item_date_add_interval :public Item_date_func
{
  String value;
  enum_field_types cached_field_type;

public:
  const interval_type int_type; // keep it public
  const bool date_sub_interval; // keep it public
  Item_date_add_interval(Item *a,Item *b,interval_type type_arg,bool neg_arg)
    :Item_date_func(a,b),int_type(type_arg), date_sub_interval(neg_arg) {}
  String *val_str(String *);
  const char *func_name() const { return "date_add_interval"; }
  void fix_length_and_dec();
  enum_field_types field_type() const { return cached_field_type; }
  int64_t val_int();
  bool get_date(DRIZZLE_TIME *res, uint32_t fuzzy_date);
  bool eq(const Item *item, bool binary_cmp) const;
  virtual void print(String *str, enum_query_type query_type);
};


class Item_extract :public Item_int_func
{
  String value;
  bool date_value;
 public:
  const interval_type int_type; // keep it public
  Item_extract(interval_type type_arg, Item *a)
    :Item_int_func(a), int_type(type_arg) {}
  int64_t val_int();
  enum Functype functype() const { return EXTRACT_FUNC; }
  const char *func_name() const { return "extract"; }
  void fix_length_and_dec();
  bool eq(const Item *item, bool binary_cmp) const;
  virtual void print(String *str, enum_query_type query_type);
};


class Item_typecast :public Item_str_func
{
public:
  Item_typecast(Item *a) :Item_str_func(a) {}
  String *val_str(String *a)
  {
    assert(fixed == 1);
    String *tmp=args[0]->val_str(a);
    null_value=args[0]->null_value;
    if (tmp)
      tmp->set_charset(collation.collation);
    return tmp;
  }
  void fix_length_and_dec()
  {
    collation.set(&my_charset_bin);
    max_length=args[0]->max_length;
  }
  virtual const char* cast_type() const= 0;
  virtual void print(String *str, enum_query_type query_type);
};


class Item_typecast_maybe_null :public Item_typecast
{
public:
  Item_typecast_maybe_null(Item *a) :Item_typecast(a) {}
  void fix_length_and_dec()
  {
    collation.set(&my_charset_bin);
    max_length=args[0]->max_length;
    maybe_null= 1;
  }
};


class Item_char_typecast :public Item_typecast
{
  int cast_length;
  const CHARSET_INFO *cast_cs, *from_cs;
  bool charset_conversion;
  String tmp_value;
public:
  Item_char_typecast(Item *a, int length_arg, const CHARSET_INFO * const cs_arg)
    :Item_typecast(a), cast_length(length_arg), cast_cs(cs_arg) {}
  enum Functype functype() const { return CHAR_TYPECAST_FUNC; }
  bool eq(const Item *item, bool binary_cmp) const;
  const char *func_name() const { return "cast_as_char"; }
  const char* cast_type() const { return "char"; };
  String *val_str(String *a);
  void fix_length_and_dec();
  virtual void print(String *str, enum_query_type query_type);
};


class Item_date_typecast :public Item_typecast_maybe_null
{
public:
  Item_date_typecast(Item *a) :Item_typecast_maybe_null(a) {}
  const char *func_name() const { return "cast_as_date"; }
  String *val_str(String *str);
  bool get_date(DRIZZLE_TIME *ltime, uint32_t fuzzy_date);
  bool get_time(DRIZZLE_TIME *ltime);
  const char *cast_type() const { return "date"; }
  enum_field_types field_type() const { return DRIZZLE_TYPE_NEWDATE; }
  Field *tmp_table_field(Table *table)
  {
    return tmp_table_field_from_field_type(table, 0);
  }  
  void fix_length_and_dec()
  {
    collation.set(&my_charset_bin);
    max_length= 10;
    maybe_null= 1;
  }
  bool result_as_int64_t() { return true; }
  int64_t val_int();
  double val_real() { return (double) val_int(); }
  my_decimal *val_decimal(my_decimal *decimal_value)
  {
    assert(fixed == 1);
    return  val_decimal_from_date(decimal_value);
  }
  int save_in_field(Field *field,
                    bool no_conversions __attribute__((unused)))
  {
    return save_date_in_field(field);
  }
};


class Item_time_typecast :public Item_typecast_maybe_null
{
public:
  Item_time_typecast(Item *a) :Item_typecast_maybe_null(a) {}
  const char *func_name() const { return "cast_as_time"; }
  String *val_str(String *str);
  bool get_time(DRIZZLE_TIME *ltime);
  const char *cast_type() const { return "time"; }
  enum_field_types field_type() const { return DRIZZLE_TYPE_TIME; }
  Field *tmp_table_field(Table *table)
  {
    return tmp_table_field_from_field_type(table, 0);
  }
  bool result_as_int64_t() { return true; }
  int64_t val_int();
  double val_real() { return val_real_from_decimal(); }
  my_decimal *val_decimal(my_decimal *decimal_value)
  {
    assert(fixed == 1);
    return  val_decimal_from_time(decimal_value);
  }
  int save_in_field(Field *field,
                    bool no_conversions __attribute__((unused)))
  {
    return save_time_in_field(field);
  }
};


class Item_datetime_typecast :public Item_typecast_maybe_null
{
public:
  Item_datetime_typecast(Item *a) :Item_typecast_maybe_null(a) {}
  const char *func_name() const { return "cast_as_datetime"; }
  String *val_str(String *str);
  const char *cast_type() const { return "datetime"; }
  enum_field_types field_type() const { return DRIZZLE_TYPE_DATETIME; }
  Field *tmp_table_field(Table *table)
  {
    return tmp_table_field_from_field_type(table, 0);
  }
  void fix_length_and_dec()
  {
    collation.set(&my_charset_bin);
    maybe_null= 1;
    max_length= MAX_DATETIME_FULL_WIDTH * MY_CHARSET_BIN_MB_MAXLEN;
    decimals= DATETIME_DEC;
  }
  bool result_as_int64_t() { return true; }
  int64_t val_int();
  double val_real() { return val_real_from_decimal(); }
  double val() { return (double) val_int(); }
  my_decimal *val_decimal(my_decimal *decimal_value)
  {
    assert(fixed == 1);
    return  val_decimal_from_date(decimal_value);
  }
  int save_in_field(Field *field,
                    bool no_conversions __attribute__((unused)))
  {
    return save_date_in_field(field);
  }
};

class Item_func_makedate :public Item_date_func
{
public:
  Item_func_makedate(Item *a,Item *b) :Item_date_func(a,b) {}
  String *val_str(String *str);
  const char *func_name() const { return "makedate"; }
  enum_field_types field_type() const { return DRIZZLE_TYPE_NEWDATE; }
  void fix_length_and_dec()
  { 
    decimals=0;
    max_length=MAX_DATE_WIDTH*MY_CHARSET_BIN_MB_MAXLEN;
  }
  int64_t val_int();
};


class Item_func_add_time :public Item_str_func
{
  const bool is_date;
  int sign;
  enum_field_types cached_field_type;

public:
  Item_func_add_time(Item *a, Item *b, bool type_arg, bool neg_arg)
    :Item_str_func(a, b), is_date(type_arg) { sign= neg_arg ? -1 : 1; }
  String *val_str(String *str);
  enum_field_types field_type() const { return cached_field_type; }
  void fix_length_and_dec();

  Field *tmp_table_field(Table *table)
  {
    return tmp_table_field_from_field_type(table, 0);
  }
  virtual void print(String *str, enum_query_type query_type);
  const char *func_name() const { return "add_time"; }
  double val_real() { return val_real_from_decimal(); }
  my_decimal *val_decimal(my_decimal *decimal_value)
  {
    assert(fixed == 1);
    if (cached_field_type == DRIZZLE_TYPE_TIME)
      return  val_decimal_from_time(decimal_value);
    if (cached_field_type == DRIZZLE_TYPE_DATETIME)
      return  val_decimal_from_date(decimal_value);
    return Item_str_func::val_decimal(decimal_value);
  }
  int save_in_field(Field *field, bool no_conversions)
  {
    if (cached_field_type == DRIZZLE_TYPE_TIME)
      return save_time_in_field(field);
    if (cached_field_type == DRIZZLE_TYPE_DATETIME)
      return save_date_in_field(field);
    return Item_str_func::save_in_field(field, no_conversions);
  }
};

class Item_func_timediff :public Item_str_timefunc
{
public:
  Item_func_timediff(Item *a, Item *b)
    :Item_str_timefunc(a, b) {}
  String *val_str(String *str);
  const char *func_name() const { return "timediff"; }
  void fix_length_and_dec()
  {
    Item_str_timefunc::fix_length_and_dec();
    maybe_null= 1;
  }
};

class Item_func_maketime :public Item_str_timefunc
{
public:
  Item_func_maketime(Item *a, Item *b, Item *c)
    :Item_str_timefunc(a, b, c) 
  {
    maybe_null= true;
  }
  String *val_str(String *str);
  const char *func_name() const { return "maketime"; }
};

class Item_func_microsecond :public Item_int_func
{
public:
  Item_func_microsecond(Item *a) :Item_int_func(a) {}
  int64_t val_int();
  const char *func_name() const { return "microsecond"; }
  void fix_length_and_dec() 
  { 
    decimals=0;
    maybe_null=1;
  }
};


class Item_func_timestamp_diff :public Item_int_func
{
  const interval_type int_type;
public:
  Item_func_timestamp_diff(Item *a,Item *b,interval_type type_arg)
    :Item_int_func(a,b), int_type(type_arg) {}
  const char *func_name() const { return "timestampdiff"; }
  int64_t val_int();
  void fix_length_and_dec()
  {
    decimals=0;
    maybe_null=1;
  }
  virtual void print(String *str, enum_query_type query_type);
};


enum date_time_format
{
  USA_FORMAT, JIS_FORMAT, ISO_FORMAT, EUR_FORMAT, INTERNAL_FORMAT
};

class Item_func_get_format :public Item_str_func
{
public:
  const enum enum_drizzle_timestamp_type type; // keep it public
  Item_func_get_format(enum enum_drizzle_timestamp_type type_arg, Item *a)
    :Item_str_func(a), type(type_arg)
  {}
  String *val_str(String *str);
  const char *func_name() const { return "get_format"; }
  void fix_length_and_dec()
  {
    maybe_null= 1;
    decimals=0;
    max_length=17*MY_CHARSET_BIN_MB_MAXLEN;
  }
  virtual void print(String *str, enum_query_type query_type);
};


class Item_func_str_to_date :public Item_str_func
{
  enum_field_types cached_field_type;
  date_time_format_types cached_format_type;
  enum enum_drizzle_timestamp_type cached_timestamp_type;
  bool const_item;
public:
  Item_func_str_to_date(Item *a, Item *b)
    :Item_str_func(a, b), const_item(false)
  {}
  String *val_str(String *str);
  bool get_date(DRIZZLE_TIME *ltime, uint32_t fuzzy_date);
  const char *func_name() const { return "str_to_date"; }
  enum_field_types field_type() const { return cached_field_type; }
  void fix_length_and_dec();
  Field *tmp_table_field(Table *table)
  {
    return tmp_table_field_from_field_type(table, 1);
  }
};


class Item_func_last_day :public Item_date
{
public:
  Item_func_last_day(Item *a) :Item_date(a) {}
  const char *func_name() const { return "last_day"; }
  bool get_date(DRIZZLE_TIME *res, uint32_t fuzzy_date);
};

#endif /* DRIZZLED_ITEM_TIMEFUNC_H */
