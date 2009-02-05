/* Copyright (C) 2000-2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/**
  @file

  @brief
  Functions to create an item. Used by sql_yac.yy
*/

#include <drizzled/server_includes.h>
#include <drizzled/item/create.h>
#include <drizzled/item/func.h>
#include <drizzled/error.h>
#include <drizzled/data_home.h>

#include <drizzled/function/str/binary.h>
#include <drizzled/function/str/concat.h>
#include <drizzled/function/str/conv.h>
#include <drizzled/function/str/elt.h>
#include <drizzled/function/str/export_set.h>
#include <drizzled/function/str/format.h>
#include <drizzled/function/str/hex.h>
#include <drizzled/function/str/load_file.h>
#include <drizzled/function/str/make_set.h>
#include <drizzled/function/str/pad.h>
#include <drizzled/function/str/repeat.h>
#include <drizzled/function/str/str_conv.h>
#include <drizzled/function/str/substr.h>
#include <drizzled/function/str/trim.h>
#include <drizzled/function/str/uuid.h>

#include <drizzled/function/time/add_time.h>
#include <drizzled/function/time/date_format.h>
#include <drizzled/function/time/dayname.h>
#include <drizzled/function/time/dayofmonth.h>
#include <drizzled/function/time/dayofyear.h>
#include <drizzled/function/time/from_unixtime.h>
#include <drizzled/function/time/from_days.h>
#include <drizzled/function/time/last_day.h>
#include <drizzled/function/time/makedate.h>
#include <drizzled/function/time/maketime.h>
#include <drizzled/function/time/month.h>
#include <drizzled/function/time/period_add.h>
#include <drizzled/function/time/period_diff.h>
#include <drizzled/function/time/sec_to_time.h>
#include <drizzled/function/time/str_to_date.h>
#include <drizzled/function/time/time_to_sec.h>
#include <drizzled/function/time/timediff.h>
#include <drizzled/function/time/to_days.h>
#include <drizzled/function/time/typecast.h>
#include <drizzled/function/time/unix_timestamp.h>
#include <drizzled/function/time/weekday.h>

#include <drizzled/item/cmpfunc.h>
#include <drizzled/sql_udf.h>
#include <drizzled/session.h>

/* Function declarations */

#include <drizzled/function/func.h>
#include <drizzled/function/math/abs.h>
#include <drizzled/function/math/acos.h>
#include <drizzled/function/additive_op.h>
#include <drizzled/function/math/asin.h>
#include <drizzled/function/math/atan.h>
#include <drizzled/function/benchmark.h>
#include <drizzled/function/bit.h>
#include <drizzled/function/bit_count.h>
#include <drizzled/function/bit_length.h>
#include <drizzled/function/math/ceiling.h>
#include <drizzled/function/char_length.h>
#include <drizzled/function/coercibility.h>
#include <drizzled/function/connection_id.h>
#include <drizzled/function/math/cos.h>
#include <drizzled/function/math/dec.h>
#include <drizzled/function/math/decimal_typecast.h>
#include <drizzled/function/math//exp.h>
#include <drizzled/function/field.h>
#include <drizzled/function/find_in_set.h>
#include <drizzled/function/math/floor.h>
#include <drizzled/function/found_rows.h>
#include <drizzled/function/get_system_var.h>
#include <drizzled/function/get_variable.h>
#include <drizzled/function/math/int_val.h>
#include <drizzled/function/math/integer.h>
#include <drizzled/function/last_insert.h>
#include <drizzled/function/length.h>
#include <drizzled/function/math/ln.h>
#include <drizzled/function/locate.h>
#include <drizzled/function/math/log.h>
#include <drizzled/function/min_max.h>
#include <drizzled/function/num1.h>
#include <drizzled/function/num_op.h>
#include <drizzled/function/numhybrid.h>
#include <drizzled/function/math/ord.h>
#include <drizzled/function/math/pow.h>
#include <drizzled/function/math/rand.h>
#include <drizzled/function/math/real.h>
#include <drizzled/function/row_count.h>
#include <drizzled/function/set_user_var.h>
#include <drizzled/function/sign.h>
#include <drizzled/function/signed.h>
#include <drizzled/function/math/sin.h>
#include <drizzled/function/math/sqrt.h>
#include <drizzled/function/str/quote.h>
#include <drizzled/function/math/tan.h>
#include <drizzled/function/units.h>
#include <drizzled/function/unsigned.h>
#include <drizzled/function/update_hash.h>


class Item;


/*
=============================================================================
  LOCAL DECLARATIONS
=============================================================================
*/

/**
  Adapter for native functions with a variable number of arguments.
  The main use of this class is to discard the following calls:
  <code>foo(expr1 AS name1, expr2 AS name2, ...)</code>
  which are syntactically correct (the syntax can refer to a UDF),
  but semantically invalid for native functions.
*/

class Create_native_func : public Create_func
{
public:
  virtual Item *create(Session *session, LEX_STRING name, List<Item> *item_list);

  /**
    Builder method, with no arguments.
    @param session The current thread
    @param name The native function name
    @param item_list The function parameters, none of which are named
    @return An item representing the function call
  */
  virtual Item *create_native(Session *session, LEX_STRING name,
                              List<Item> *item_list) = 0;

protected:
  /** Constructor. */
  Create_native_func() {}
  /** Destructor. */
  virtual ~Create_native_func() {}
};


/**
  Adapter for functions that takes exactly zero arguments.
*/

class Create_func_arg0 : public Create_func
{
public:
  virtual Item *create(Session *session, LEX_STRING name, List<Item> *item_list);

  /**
    Builder method, with no arguments.
    @param session The current thread
    @return An item representing the function call
  */
  virtual Item *create(Session *session) = 0;

protected:
  /** Constructor. */
  Create_func_arg0() {}
  /** Destructor. */
  virtual ~Create_func_arg0() {}
};


/**
  Adapter for functions that takes exactly one argument.
*/

class Create_func_arg1 : public Create_func
{
public:
  virtual Item *create(Session *session, LEX_STRING name, List<Item> *item_list);

  /**
    Builder method, with one argument.
    @param session The current thread
    @param arg1 The first argument of the function
    @return An item representing the function call
  */
  virtual Item *create(Session *session, Item *arg1) = 0;

protected:
  /** Constructor. */
  Create_func_arg1() {}
  /** Destructor. */
  virtual ~Create_func_arg1() {}
};


/**
  Adapter for functions that takes exactly two arguments.
*/

class Create_func_arg2 : public Create_func
{
public:
  virtual Item *create(Session *session, LEX_STRING name, List<Item> *item_list);

  /**
    Builder method, with two arguments.
    @param session The current thread
    @param arg1 The first argument of the function
    @param arg2 The second argument of the function
    @return An item representing the function call
  */
  virtual Item *create(Session *session, Item *arg1, Item *arg2) = 0;

protected:
  /** Constructor. */
  Create_func_arg2() {}
  /** Destructor. */
  virtual ~Create_func_arg2() {}
};


/**
  Adapter for functions that takes exactly three arguments.
*/

class Create_func_arg3 : public Create_func
{
public:
  virtual Item *create(Session *session, LEX_STRING name, List<Item> *item_list);

  /**
    Builder method, with three arguments.
    @param session The current thread
    @param arg1 The first argument of the function
    @param arg2 The second argument of the function
    @param arg3 The third argument of the function
    @return An item representing the function call
  */
  virtual Item *create(Session *session, Item *arg1, Item *arg2, Item *arg3) = 0;

protected:
  /** Constructor. */
  Create_func_arg3() {}
  /** Destructor. */
  virtual ~Create_func_arg3() {}
};


/**
  Function builder for Stored Functions.
*/

/*
  Concrete functions builders (native functions).
  Please keep this list sorted in alphabetical order,
  it helps to compare code between versions, and helps with merges conflicts.
*/

class Create_func_abs : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_abs s_singleton;

protected:
  Create_func_abs() {}
  virtual ~Create_func_abs() {}
};


class Create_func_acos : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_acos s_singleton;

protected:
  Create_func_acos() {}
  virtual ~Create_func_acos() {}
};


class Create_func_addtime : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_addtime s_singleton;

protected:
  Create_func_addtime() {}
  virtual ~Create_func_addtime() {}
};


class Create_func_asin : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_asin s_singleton;

protected:
  Create_func_asin() {}
  virtual ~Create_func_asin() {}
};


class Create_func_atan : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_atan s_singleton;

protected:
  Create_func_atan() {}
  virtual ~Create_func_atan() {}
};


class Create_func_benchmark : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_benchmark s_singleton;

protected:
  Create_func_benchmark() {}
  virtual ~Create_func_benchmark() {}
};


class Create_func_bin : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_bin s_singleton;

protected:
  Create_func_bin() {}
  virtual ~Create_func_bin() {}
};


class Create_func_bit_count : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_bit_count s_singleton;

protected:
  Create_func_bit_count() {}
  virtual ~Create_func_bit_count() {}
};


class Create_func_bit_length : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_bit_length s_singleton;

protected:
  Create_func_bit_length() {}
  virtual ~Create_func_bit_length() {}
};


class Create_func_ceiling : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_ceiling s_singleton;

protected:
  Create_func_ceiling() {}
  virtual ~Create_func_ceiling() {}
};


class Create_func_char_length : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_char_length s_singleton;

protected:
  Create_func_char_length() {}
  virtual ~Create_func_char_length() {}
};


class Create_func_coercibility : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_coercibility s_singleton;

protected:
  Create_func_coercibility() {}
  virtual ~Create_func_coercibility() {}
};


class Create_func_concat : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_concat s_singleton;

protected:
  Create_func_concat() {}
  virtual ~Create_func_concat() {}
};


class Create_func_concat_ws : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_concat_ws s_singleton;

protected:
  Create_func_concat_ws() {}
  virtual ~Create_func_concat_ws() {}
};


class Create_func_connection_id : public Create_func_arg0
{
public:
  virtual Item *create(Session *session);

  static Create_func_connection_id s_singleton;

protected:
  Create_func_connection_id() {}
  virtual ~Create_func_connection_id() {}
};


class Create_func_conv : public Create_func_arg3
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2, Item *arg3);

  static Create_func_conv s_singleton;

protected:
  Create_func_conv() {}
  virtual ~Create_func_conv() {}
};


class Create_func_cos : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_cos s_singleton;

protected:
  Create_func_cos() {}
  virtual ~Create_func_cos() {}
};


class Create_func_cot : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_cot s_singleton;

protected:
  Create_func_cot() {}
  virtual ~Create_func_cot() {}
};

class Create_func_date_format : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_date_format s_singleton;

protected:
  Create_func_date_format() {}
  virtual ~Create_func_date_format() {}
};


class Create_func_datediff : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_datediff s_singleton;

protected:
  Create_func_datediff() {}
  virtual ~Create_func_datediff() {}
};


class Create_func_dayname : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_dayname s_singleton;

protected:
  Create_func_dayname() {}
  virtual ~Create_func_dayname() {}
};


class Create_func_dayofmonth : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_dayofmonth s_singleton;

protected:
  Create_func_dayofmonth() {}
  virtual ~Create_func_dayofmonth() {}
};


class Create_func_dayofweek : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_dayofweek s_singleton;

protected:
  Create_func_dayofweek() {}
  virtual ~Create_func_dayofweek() {}
};


class Create_func_dayofyear : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_dayofyear s_singleton;

protected:
  Create_func_dayofyear() {}
  virtual ~Create_func_dayofyear() {}
};


class Create_func_decode : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_decode s_singleton;

protected:
  Create_func_decode() {}
  virtual ~Create_func_decode() {}
};


class Create_func_degrees : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_degrees s_singleton;

protected:
  Create_func_degrees() {}
  virtual ~Create_func_degrees() {}
};


class Create_func_elt : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_elt s_singleton;

protected:
  Create_func_elt() {}
  virtual ~Create_func_elt() {}
};


class Create_func_exp : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_exp s_singleton;

protected:
  Create_func_exp() {}
  virtual ~Create_func_exp() {}
};


class Create_func_export_set : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_export_set s_singleton;

protected:
  Create_func_export_set() {}
  virtual ~Create_func_export_set() {}
};


class Create_func_field : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_field s_singleton;

protected:
  Create_func_field() {}
  virtual ~Create_func_field() {}
};


class Create_func_find_in_set : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_find_in_set s_singleton;

protected:
  Create_func_find_in_set() {}
  virtual ~Create_func_find_in_set() {}
};


class Create_func_floor : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_floor s_singleton;

protected:
  Create_func_floor() {}
  virtual ~Create_func_floor() {}
};


class Create_func_format : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_format s_singleton;

protected:
  Create_func_format() {}
  virtual ~Create_func_format() {}
};


class Create_func_found_rows : public Create_func_arg0
{
public:
  virtual Item *create(Session *session);

  static Create_func_found_rows s_singleton;

protected:
  Create_func_found_rows() {}
  virtual ~Create_func_found_rows() {}
};


class Create_func_from_days : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_from_days s_singleton;

protected:
  Create_func_from_days() {}
  virtual ~Create_func_from_days() {}
};


class Create_func_from_unixtime : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_from_unixtime s_singleton;

protected:
  Create_func_from_unixtime() {}
  virtual ~Create_func_from_unixtime() {}
};


class Create_func_greatest : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_greatest s_singleton;

protected:
  Create_func_greatest() {}
  virtual ~Create_func_greatest() {}
};


class Create_func_hex : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_hex s_singleton;

protected:
  Create_func_hex() {}
  virtual ~Create_func_hex() {}
};


class Create_func_ifnull : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_ifnull s_singleton;

protected:
  Create_func_ifnull() {}
  virtual ~Create_func_ifnull() {}
};


class Create_func_instr : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_instr s_singleton;

protected:
  Create_func_instr() {}
  virtual ~Create_func_instr() {}
};


class Create_func_isnull : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_isnull s_singleton;

protected:
  Create_func_isnull() {}
  virtual ~Create_func_isnull() {}
};


class Create_func_last_day : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_last_day s_singleton;

protected:
  Create_func_last_day() {}
  virtual ~Create_func_last_day() {}
};


class Create_func_last_insert_id : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_last_insert_id s_singleton;

protected:
  Create_func_last_insert_id() {}
  virtual ~Create_func_last_insert_id() {}
};


class Create_func_lcase : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_lcase s_singleton;

protected:
  Create_func_lcase() {}
  virtual ~Create_func_lcase() {}
};


class Create_func_least : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_least s_singleton;

protected:
  Create_func_least() {}
  virtual ~Create_func_least() {}
};


class Create_func_length : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_length s_singleton;

protected:
  Create_func_length() {}
  virtual ~Create_func_length() {}
};


class Create_func_ln : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_ln s_singleton;

protected:
  Create_func_ln() {}
  virtual ~Create_func_ln() {}
};


class Create_func_load_file : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_load_file s_singleton;

protected:
  Create_func_load_file() {}
  virtual ~Create_func_load_file() {}
};


class Create_func_locate : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_locate s_singleton;

protected:
  Create_func_locate() {}
  virtual ~Create_func_locate() {}
};


class Create_func_log : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_log s_singleton;

protected:
  Create_func_log() {}
  virtual ~Create_func_log() {}
};


class Create_func_log10 : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_log10 s_singleton;

protected:
  Create_func_log10() {}
  virtual ~Create_func_log10() {}
};


class Create_func_log2 : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_log2 s_singleton;

protected:
  Create_func_log2() {}
  virtual ~Create_func_log2() {}
};


class Create_func_lpad : public Create_func_arg3
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2, Item *arg3);

  static Create_func_lpad s_singleton;

protected:
  Create_func_lpad() {}
  virtual ~Create_func_lpad() {}
};


class Create_func_ltrim : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_ltrim s_singleton;

protected:
  Create_func_ltrim() {}
  virtual ~Create_func_ltrim() {}
};


class Create_func_makedate : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_makedate s_singleton;

protected:
  Create_func_makedate() {}
  virtual ~Create_func_makedate() {}
};


class Create_func_maketime : public Create_func_arg3
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2, Item *arg3);

  static Create_func_maketime s_singleton;

protected:
  Create_func_maketime() {}
  virtual ~Create_func_maketime() {}
};


class Create_func_make_set : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_make_set s_singleton;

protected:
  Create_func_make_set() {}
  virtual ~Create_func_make_set() {}
};


class Create_func_monthname : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_monthname s_singleton;

protected:
  Create_func_monthname() {}
  virtual ~Create_func_monthname() {}
};


class Create_func_name_const : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_name_const s_singleton;

protected:
  Create_func_name_const() {}
  virtual ~Create_func_name_const() {}
};


class Create_func_nullif : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_nullif s_singleton;

protected:
  Create_func_nullif() {}
  virtual ~Create_func_nullif() {}
};


class Create_func_oct : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_oct s_singleton;

protected:
  Create_func_oct() {}
  virtual ~Create_func_oct() {}
};


class Create_func_ord : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_ord s_singleton;

protected:
  Create_func_ord() {}
  virtual ~Create_func_ord() {}
};


class Create_func_period_add : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_period_add s_singleton;

protected:
  Create_func_period_add() {}
  virtual ~Create_func_period_add() {}
};


class Create_func_period_diff : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_period_diff s_singleton;

protected:
  Create_func_period_diff() {}
  virtual ~Create_func_period_diff() {}
};


class Create_func_pi : public Create_func_arg0
{
public:
  virtual Item *create(Session *session);

  static Create_func_pi s_singleton;

protected:
  Create_func_pi() {}
  virtual ~Create_func_pi() {}
};


class Create_func_pow : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_pow s_singleton;

protected:
  Create_func_pow() {}
  virtual ~Create_func_pow() {}
};


class Create_func_quote : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_quote s_singleton;

protected:
  Create_func_quote() {}
  virtual ~Create_func_quote() {}
};


class Create_func_radians : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_radians s_singleton;

protected:
  Create_func_radians() {}
  virtual ~Create_func_radians() {}
};


class Create_func_rand : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_rand s_singleton;

protected:
  Create_func_rand() {}
  virtual ~Create_func_rand() {}
};


class Create_func_round : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_round s_singleton;

protected:
  Create_func_round() {}
  virtual ~Create_func_round() {}
};


class Create_func_row_count : public Create_func_arg0
{
public:
  virtual Item *create(Session *session);

  static Create_func_row_count s_singleton;

protected:
  Create_func_row_count() {}
  virtual ~Create_func_row_count() {}
};


class Create_func_rpad : public Create_func_arg3
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2, Item *arg3);

  static Create_func_rpad s_singleton;

protected:
  Create_func_rpad() {}
  virtual ~Create_func_rpad() {}
};


class Create_func_rtrim : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_rtrim s_singleton;

protected:
  Create_func_rtrim() {}
  virtual ~Create_func_rtrim() {}
};


class Create_func_sec_to_time : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_sec_to_time s_singleton;

protected:
  Create_func_sec_to_time() {}
  virtual ~Create_func_sec_to_time() {}
};


class Create_func_sign : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_sign s_singleton;

protected:
  Create_func_sign() {}
  virtual ~Create_func_sign() {}
};


class Create_func_sin : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_sin s_singleton;

protected:
  Create_func_sin() {}
  virtual ~Create_func_sin() {}
};


class Create_func_space : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_space s_singleton;

protected:
  Create_func_space() {}
  virtual ~Create_func_space() {}
};


class Create_func_sqrt : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_sqrt s_singleton;

protected:
  Create_func_sqrt() {}
  virtual ~Create_func_sqrt() {}
};


class Create_func_str_to_date : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_str_to_date s_singleton;

protected:
  Create_func_str_to_date() {}
  virtual ~Create_func_str_to_date() {}
};


class Create_func_strcmp : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_strcmp s_singleton;

protected:
  Create_func_strcmp() {}
  virtual ~Create_func_strcmp() {}
};


class Create_func_substr_index : public Create_func_arg3
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2, Item *arg3);

  static Create_func_substr_index s_singleton;

protected:
  Create_func_substr_index() {}
  virtual ~Create_func_substr_index() {}
};


class Create_func_subtime : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_subtime s_singleton;

protected:
  Create_func_subtime() {}
  virtual ~Create_func_subtime() {}
};


class Create_func_tan : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_tan s_singleton;

protected:
  Create_func_tan() {}
  virtual ~Create_func_tan() {}
};


class Create_func_time_format : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_time_format s_singleton;

protected:
  Create_func_time_format() {}
  virtual ~Create_func_time_format() {}
};


class Create_func_time_to_sec : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_time_to_sec s_singleton;

protected:
  Create_func_time_to_sec() {}
  virtual ~Create_func_time_to_sec() {}
};


class Create_func_timediff : public Create_func_arg2
{
public:
  virtual Item *create(Session *session, Item *arg1, Item *arg2);

  static Create_func_timediff s_singleton;

protected:
  Create_func_timediff() {}
  virtual ~Create_func_timediff() {}
};


class Create_func_to_days : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_to_days s_singleton;

protected:
  Create_func_to_days() {}
  virtual ~Create_func_to_days() {}
};


class Create_func_ucase : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_ucase s_singleton;

protected:
  Create_func_ucase() {}
  virtual ~Create_func_ucase() {}
};


class Create_func_unhex : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_unhex s_singleton;

protected:
  Create_func_unhex() {}
  virtual ~Create_func_unhex() {}
};


class Create_func_unix_timestamp : public Create_native_func
{
public:
  virtual Item *create_native(Session *session, LEX_STRING name, List<Item> *item_list);

  static Create_func_unix_timestamp s_singleton;

protected:
  Create_func_unix_timestamp() {}
  virtual ~Create_func_unix_timestamp() {}
};


class Create_func_uuid : public Create_func_arg0
{
public:
  virtual Item *create(Session *session);

  static Create_func_uuid s_singleton;

protected:
  Create_func_uuid() {}
  virtual ~Create_func_uuid() {}
};


class Create_func_version : public Create_func_arg0
{
public:
  virtual Item *create(Session *session);

  static Create_func_version s_singleton;

protected:
  Create_func_version() {}
  virtual ~Create_func_version() {}
};


class Create_func_weekday : public Create_func_arg1
{
public:
  virtual Item *create(Session *session, Item *arg1);

  static Create_func_weekday s_singleton;

protected:
  Create_func_weekday() {}
  virtual ~Create_func_weekday() {}
};

/*
=============================================================================
  IMPLEMENTATION
=============================================================================
*/

/**
  Checks if there are named parameters in a parameter list.
  The syntax to name parameters in a function call is as follow:
  <code>foo(expr AS named, expr named, expr AS "named", expr "named")</code>
  @param params The parameter list, can be null
  @return true if one or more parameter is named
*/
static bool has_named_parameters(List<Item> *params)
{
  if (params)
  {
    Item *param;
    List_iterator<Item> it(*params);
    while ((param= it++))
    {
      if (! param->is_autogenerated_name)
        return true;
    }
  }

  return false;
}


Create_udf_func Create_udf_func::s_singleton;

Item*
Create_udf_func::create(Session *session, LEX_STRING name, List<Item> *item_list)
{
  udf_func *udf= find_udf(name.str, name.length);
  assert(udf);
  return create(session, udf, item_list);
}


Item*
Create_udf_func::create(Session *session, udf_func *udf, List<Item> *item_list)
{
  Item_func *func= NULL;
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  func= udf->create_func(session->mem_root);

  func->set_arguments(*item_list);

  return func;
}


Item*
Create_native_func::create(Session *session, LEX_STRING name, List<Item> *item_list)
{
  if (has_named_parameters(item_list))
  {
    my_error(ER_WRONG_PARAMETERS_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  return create_native(session, name, item_list);
}


Item*
Create_func_arg0::create(Session *session, LEX_STRING name, List<Item> *item_list)
{
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  if (arg_count != 0)
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  return create(session);
}


Item*
Create_func_arg1::create(Session *session, LEX_STRING name, List<Item> *item_list)
{
  int arg_count= 0;

  if (item_list)
    arg_count= item_list->elements;

  if (arg_count != 1)
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  Item *param_1= item_list->pop();

  if (! param_1->is_autogenerated_name)
  {
    my_error(ER_WRONG_PARAMETERS_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  return create(session, param_1);
}


Item*
Create_func_arg2::create(Session *session, LEX_STRING name, List<Item> *item_list)
{
  int arg_count= 0;

  if (item_list)
    arg_count= item_list->elements;

  if (arg_count != 2)
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  Item *param_1= item_list->pop();
  Item *param_2= item_list->pop();

  if (   (! param_1->is_autogenerated_name)
      || (! param_2->is_autogenerated_name))
  {
    my_error(ER_WRONG_PARAMETERS_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  return create(session, param_1, param_2);
}


Item*
Create_func_arg3::create(Session *session, LEX_STRING name, List<Item> *item_list)
{
  int arg_count= 0;

  if (item_list)
    arg_count= item_list->elements;

  if (arg_count != 3)
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  Item *param_1= item_list->pop();
  Item *param_2= item_list->pop();
  Item *param_3= item_list->pop();

  if (   (! param_1->is_autogenerated_name)
      || (! param_2->is_autogenerated_name)
      || (! param_3->is_autogenerated_name))
  {
    my_error(ER_WRONG_PARAMETERS_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  return create(session, param_1, param_2, param_3);
}


Create_func_abs Create_func_abs::s_singleton;

Item*
Create_func_abs::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_abs(arg1);
}


Create_func_acos Create_func_acos::s_singleton;

Item*
Create_func_acos::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_acos(arg1);
}


Create_func_addtime Create_func_addtime::s_singleton;

Item*
Create_func_addtime::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_add_time(arg1, arg2, 0, 0);
}


Create_func_asin Create_func_asin::s_singleton;

Item*
Create_func_asin::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_asin(arg1);
}


Create_func_atan Create_func_atan::s_singleton;

Item*
Create_func_atan::create_native(Session *session, LEX_STRING name,
                                List<Item> *item_list)
{
  Item* func= NULL;
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  switch (arg_count) {
  case 1:
  {
    Item *param_1= item_list->pop();
    func= new (session->mem_root) Item_func_atan(param_1);
    break;
  }
  case 2:
  {
    Item *param_1= item_list->pop();
    Item *param_2= item_list->pop();
    func= new (session->mem_root) Item_func_atan(param_1, param_2);
    break;
  }
  default:
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    break;
  }
  }

  return func;
}


Create_func_benchmark Create_func_benchmark::s_singleton;

Item*
Create_func_benchmark::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_benchmark(arg1, arg2);
}


Create_func_bin Create_func_bin::s_singleton;

Item*
Create_func_bin::create(Session *session, Item *arg1)
{
  Item *i10= new (session->mem_root) Item_int((int32_t) 10,2);
  Item *i2= new (session->mem_root) Item_int((int32_t) 2,1);
  return new (session->mem_root) Item_func_conv(arg1, i10, i2);
}


Create_func_bit_count Create_func_bit_count::s_singleton;

Item*
Create_func_bit_count::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_bit_count(arg1);
}


Create_func_bit_length Create_func_bit_length::s_singleton;

Item*
Create_func_bit_length::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_bit_length(arg1);
}


Create_func_ceiling Create_func_ceiling::s_singleton;

Item*
Create_func_ceiling::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_ceiling(arg1);
}


Create_func_char_length Create_func_char_length::s_singleton;

Item*
Create_func_char_length::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_char_length(arg1);
}


Create_func_coercibility Create_func_coercibility::s_singleton;

Item*
Create_func_coercibility::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_coercibility(arg1);
}


Create_func_concat Create_func_concat::s_singleton;

Item*
Create_func_concat::create_native(Session *session, LEX_STRING name,
                                  List<Item> *item_list)
{
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  if (arg_count < 1)
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  return new (session->mem_root) Item_func_concat(*item_list);
}


Create_func_concat_ws Create_func_concat_ws::s_singleton;

Item*
Create_func_concat_ws::create_native(Session *session, LEX_STRING name,
                                     List<Item> *item_list)
{
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  /* "WS" stands for "With Separator": this function takes 2+ arguments */
  if (arg_count < 2)
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  return new (session->mem_root) Item_func_concat_ws(*item_list);
}


Create_func_connection_id Create_func_connection_id::s_singleton;

Item*
Create_func_connection_id::create(Session *session)
{
  return new (session->mem_root) Item_func_connection_id();
}


Create_func_conv Create_func_conv::s_singleton;

Item*
Create_func_conv::create(Session *session, Item *arg1, Item *arg2, Item *arg3)
{
  return new (session->mem_root) Item_func_conv(arg1, arg2, arg3);
}


Create_func_cos Create_func_cos::s_singleton;

Item*
Create_func_cos::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_cos(arg1);
}


Create_func_cot Create_func_cot::s_singleton;

Item*
Create_func_cot::create(Session *session, Item *arg1)
{
  Item *i1= new (session->mem_root) Item_int((char*) "1", 1, 1);
  Item *i2= new (session->mem_root) Item_func_tan(arg1);
  return new (session->mem_root) Item_func_div(i1, i2);
}

Create_func_date_format Create_func_date_format::s_singleton;

Item*
Create_func_date_format::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_date_format(arg1, arg2, 0);
}


Create_func_datediff Create_func_datediff::s_singleton;

Item*
Create_func_datediff::create(Session *session, Item *arg1, Item *arg2)
{
  Item *i1= new (session->mem_root) Item_func_to_days(arg1);
  Item *i2= new (session->mem_root) Item_func_to_days(arg2);

  return new (session->mem_root) Item_func_minus(i1, i2);
}


Create_func_dayname Create_func_dayname::s_singleton;

Item*
Create_func_dayname::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_dayname(arg1);
}


Create_func_dayofmonth Create_func_dayofmonth::s_singleton;

Item*
Create_func_dayofmonth::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_dayofmonth(arg1);
}


Create_func_dayofweek Create_func_dayofweek::s_singleton;

Item*
Create_func_dayofweek::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_weekday(arg1, 1);
}


Create_func_dayofyear Create_func_dayofyear::s_singleton;

Item*
Create_func_dayofyear::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_dayofyear(arg1);
}


Create_func_degrees Create_func_degrees::s_singleton;

Item*
Create_func_degrees::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_units((char*) "degrees", arg1,
                                             180/M_PI, 0.0);
}


Create_func_elt Create_func_elt::s_singleton;

Item*
Create_func_elt::create_native(Session *session, LEX_STRING name,
                               List<Item> *item_list)
{
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  if (arg_count < 2)
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  return new (session->mem_root) Item_func_elt(*item_list);
}


Create_func_exp Create_func_exp::s_singleton;

Item*
Create_func_exp::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_exp(arg1);
}


Create_func_export_set Create_func_export_set::s_singleton;

Item*
Create_func_export_set::create_native(Session *session, LEX_STRING name,
                                      List<Item> *item_list)
{
  Item *func= NULL;
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  switch (arg_count) {
  case 3:
  {
    Item *param_1= item_list->pop();
    Item *param_2= item_list->pop();
    Item *param_3= item_list->pop();
    func= new (session->mem_root) Item_func_export_set(param_1, param_2, param_3);
    break;
  }
  case 4:
  {
    Item *param_1= item_list->pop();
    Item *param_2= item_list->pop();
    Item *param_3= item_list->pop();
    Item *param_4= item_list->pop();
    func= new (session->mem_root) Item_func_export_set(param_1, param_2, param_3,
                                                   param_4);
    break;
  }
  case 5:
  {
    Item *param_1= item_list->pop();
    Item *param_2= item_list->pop();
    Item *param_3= item_list->pop();
    Item *param_4= item_list->pop();
    Item *param_5= item_list->pop();
    func= new (session->mem_root) Item_func_export_set(param_1, param_2, param_3,
                                                   param_4, param_5);
    break;
  }
  default:
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    break;
  }
  }

  return func;
}


Create_func_field Create_func_field::s_singleton;

Item*
Create_func_field::create_native(Session *session, LEX_STRING name,
                                 List<Item> *item_list)
{
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  if (arg_count < 2)
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  return new (session->mem_root) Item_func_field(*item_list);
}


Create_func_find_in_set Create_func_find_in_set::s_singleton;

Item*
Create_func_find_in_set::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_find_in_set(arg1, arg2);
}


Create_func_floor Create_func_floor::s_singleton;

Item*
Create_func_floor::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_floor(arg1);
}


Create_func_format Create_func_format::s_singleton;

Item*
Create_func_format::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_format(arg1, arg2);
}


Create_func_found_rows Create_func_found_rows::s_singleton;

Item*
Create_func_found_rows::create(Session *session)
{
  return new (session->mem_root) Item_func_found_rows();
}


Create_func_from_days Create_func_from_days::s_singleton;

Item*
Create_func_from_days::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_from_days(arg1);
}


Create_func_from_unixtime Create_func_from_unixtime::s_singleton;

Item*
Create_func_from_unixtime::create_native(Session *session, LEX_STRING name,
                                         List<Item> *item_list)
{
  Item *func= NULL;
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  switch (arg_count) {
  case 1:
  {
    Item *param_1= item_list->pop();
    func= new (session->mem_root) Item_func_from_unixtime(param_1);
    break;
  }
  case 2:
  {
    Item *param_1= item_list->pop();
    Item *param_2= item_list->pop();
    Item *ut= new (session->mem_root) Item_func_from_unixtime(param_1);
    func= new (session->mem_root) Item_func_date_format(ut, param_2, 0);
    break;
  }
  default:
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    break;
  }
  }

  return func;
}


Create_func_greatest Create_func_greatest::s_singleton;

Item*
Create_func_greatest::create_native(Session *session, LEX_STRING name,
                                    List<Item> *item_list)
{
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  if (arg_count < 2)
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  return new (session->mem_root) Item_func_max(*item_list);
}


Create_func_hex Create_func_hex::s_singleton;

Item*
Create_func_hex::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_hex(arg1);
}


Create_func_ifnull Create_func_ifnull::s_singleton;

Item*
Create_func_ifnull::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_ifnull(arg1, arg2);
}


Create_func_instr Create_func_instr::s_singleton;

Item*
Create_func_instr::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_locate(arg1, arg2);
}


Create_func_isnull Create_func_isnull::s_singleton;

Item*
Create_func_isnull::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_isnull(arg1);
}


Create_func_last_day Create_func_last_day::s_singleton;

Item*
Create_func_last_day::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_last_day(arg1);
}


Create_func_last_insert_id Create_func_last_insert_id::s_singleton;

Item*
Create_func_last_insert_id::create_native(Session *session, LEX_STRING name,
                                          List<Item> *item_list)
{
  Item *func= NULL;
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  switch (arg_count) {
  case 0:
  {
    func= new (session->mem_root) Item_func_last_insert_id();
    break;
  }
  case 1:
  {
    Item *param_1= item_list->pop();
    func= new (session->mem_root) Item_func_last_insert_id(param_1);
    break;
  }
  default:
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    break;
  }
  }

  return func;
}


Create_func_lcase Create_func_lcase::s_singleton;

Item*
Create_func_lcase::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_lcase(arg1);
}


Create_func_least Create_func_least::s_singleton;

Item*
Create_func_least::create_native(Session *session, LEX_STRING name,
                                 List<Item> *item_list)
{
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  if (arg_count < 2)
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  return new (session->mem_root) Item_func_min(*item_list);
}


Create_func_length Create_func_length::s_singleton;

Item*
Create_func_length::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_length(arg1);
}


Create_func_ln Create_func_ln::s_singleton;

Item*
Create_func_ln::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_ln(arg1);
}


Create_func_load_file Create_func_load_file::s_singleton;

Item*
Create_func_load_file::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_load_file(arg1);
}


Create_func_locate Create_func_locate::s_singleton;

Item*
Create_func_locate::create_native(Session *session, LEX_STRING name,
                                  List<Item> *item_list)
{
  Item *func= NULL;
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  switch (arg_count) {
  case 2:
  {
    Item *param_1= item_list->pop();
    Item *param_2= item_list->pop();
    /* Yes, parameters in that order : 2, 1 */
    func= new (session->mem_root) Item_func_locate(param_2, param_1);
    break;
  }
  case 3:
  {
    Item *param_1= item_list->pop();
    Item *param_2= item_list->pop();
    Item *param_3= item_list->pop();
    /* Yes, parameters in that order : 2, 1, 3 */
    func= new (session->mem_root) Item_func_locate(param_2, param_1, param_3);
    break;
  }
  default:
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    break;
  }
  }

  return func;
}


Create_func_log Create_func_log::s_singleton;

Item*
Create_func_log::create_native(Session *session, LEX_STRING name,
                               List<Item> *item_list)
{
  Item *func= NULL;
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  switch (arg_count) {
  case 1:
  {
    Item *param_1= item_list->pop();
    func= new (session->mem_root) Item_func_log(param_1);
    break;
  }
  case 2:
  {
    Item *param_1= item_list->pop();
    Item *param_2= item_list->pop();
    func= new (session->mem_root) Item_func_log(param_1, param_2);
    break;
  }
  default:
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    break;
  }
  }

  return func;
}


Create_func_log10 Create_func_log10::s_singleton;

Item*
Create_func_log10::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_log10(arg1);
}


Create_func_log2 Create_func_log2::s_singleton;

Item*
Create_func_log2::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_log2(arg1);
}


Create_func_lpad Create_func_lpad::s_singleton;

Item*
Create_func_lpad::create(Session *session, Item *arg1, Item *arg2, Item *arg3)
{
  return new (session->mem_root) Item_func_lpad(arg1, arg2, arg3);
}


Create_func_ltrim Create_func_ltrim::s_singleton;

Item*
Create_func_ltrim::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_ltrim(arg1);
}


Create_func_makedate Create_func_makedate::s_singleton;

Item*
Create_func_makedate::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_makedate(arg1, arg2);
}


Create_func_maketime Create_func_maketime::s_singleton;

Item*
Create_func_maketime::create(Session *session, Item *arg1, Item *arg2, Item *arg3)
{
  return new (session->mem_root) Item_func_maketime(arg1, arg2, arg3);
}


Create_func_make_set Create_func_make_set::s_singleton;

Item*
Create_func_make_set::create_native(Session *session, LEX_STRING name,
                                    List<Item> *item_list)
{
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  if (arg_count < 2)
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    return NULL;
  }

  Item *param_1= item_list->pop();
  return new (session->mem_root) Item_func_make_set(param_1, *item_list);
}


Create_func_monthname Create_func_monthname::s_singleton;

Item*
Create_func_monthname::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_monthname(arg1);
}


Create_func_nullif Create_func_nullif::s_singleton;

Item*
Create_func_nullif::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_nullif(arg1, arg2);
}


Create_func_oct Create_func_oct::s_singleton;

Item*
Create_func_oct::create(Session *session, Item *arg1)
{
  Item *i10= new (session->mem_root) Item_int((int32_t) 10,2);
  Item *i8= new (session->mem_root) Item_int((int32_t) 8,1);
  return new (session->mem_root) Item_func_conv(arg1, i10, i8);
}


Create_func_ord Create_func_ord::s_singleton;

Item*
Create_func_ord::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_ord(arg1);
}


Create_func_period_add Create_func_period_add::s_singleton;

Item*
Create_func_period_add::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_period_add(arg1, arg2);
}


Create_func_period_diff Create_func_period_diff::s_singleton;

Item*
Create_func_period_diff::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_period_diff(arg1, arg2);
}


Create_func_pi Create_func_pi::s_singleton;

Item*
Create_func_pi::create(Session *session)
{
  return new (session->mem_root) Item_static_float_func("pi()", M_PI, 6, 8);
}


Create_func_pow Create_func_pow::s_singleton;

Item*
Create_func_pow::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_pow(arg1, arg2);
}


Create_func_quote Create_func_quote::s_singleton;

Item*
Create_func_quote::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_quote(arg1);
}


Create_func_radians Create_func_radians::s_singleton;

Item*
Create_func_radians::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_units((char*) "radians", arg1,
                                             M_PI/180, 0.0);
}


Create_func_rand Create_func_rand::s_singleton;

Item*
Create_func_rand::create_native(Session *session, LEX_STRING name,
                                List<Item> *item_list)
{
  Item *func= NULL;
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  switch (arg_count) {
  case 0:
  {
    func= new (session->mem_root) Item_func_rand();
    break;
  }
  case 1:
  {
    Item *param_1= item_list->pop();
    func= new (session->mem_root) Item_func_rand(param_1);
    break;
  }
  default:
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    break;
  }
  }

  return func;
}


Create_func_round Create_func_round::s_singleton;

Item*
Create_func_round::create_native(Session *session, LEX_STRING name,
                                 List<Item> *item_list)
{
  Item *func= NULL;
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  switch (arg_count) {
  case 1:
  {
    Item *param_1= item_list->pop();
    Item *i0 = new (session->mem_root) Item_int((char*)"0", 0, 1);
    func= new (session->mem_root) Item_func_round(param_1, i0, 0);
    break;
  }
  case 2:
  {
    Item *param_1= item_list->pop();
    Item *param_2= item_list->pop();
    func= new (session->mem_root) Item_func_round(param_1, param_2, 0);
    break;
  }
  default:
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    break;
  }
  }

  return func;
}


Create_func_row_count Create_func_row_count::s_singleton;

Item*
Create_func_row_count::create(Session *session)
{
  return new (session->mem_root) Item_func_row_count();
}


Create_func_rpad Create_func_rpad::s_singleton;

Item*
Create_func_rpad::create(Session *session, Item *arg1, Item *arg2, Item *arg3)
{
  return new (session->mem_root) Item_func_rpad(arg1, arg2, arg3);
}


Create_func_rtrim Create_func_rtrim::s_singleton;

Item*
Create_func_rtrim::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_rtrim(arg1);
}


Create_func_sec_to_time Create_func_sec_to_time::s_singleton;

Item*
Create_func_sec_to_time::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_sec_to_time(arg1);
}


Create_func_sign Create_func_sign::s_singleton;

Item*
Create_func_sign::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_sign(arg1);
}


Create_func_sin Create_func_sin::s_singleton;

Item*
Create_func_sin::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_sin(arg1);
}


Create_func_space Create_func_space::s_singleton;

Item*
Create_func_space::create(Session *session, Item *arg1)
{
  /**
    TODO: Fix Bug#23637
    The parsed item tree should not depend on
    <code>session->variables.collation_connection</code>.
  */
  const CHARSET_INFO * const cs= session->variables.getCollation();
  Item *sp;

  if (cs->mbminlen > 1)
  {
    uint32_t dummy_errors;
    sp= new (session->mem_root) Item_string("", 0, cs, DERIVATION_COERCIBLE, MY_REPERTOIRE_ASCII);
    sp->str_value.copy(" ", 1, &my_charset_utf8_general_ci, cs, &dummy_errors);
  }
  else
  {
    sp= new (session->mem_root) Item_string(" ", 1, cs, DERIVATION_COERCIBLE, MY_REPERTOIRE_ASCII);
  }

  return new (session->mem_root) Item_func_repeat(sp, arg1);
}


Create_func_sqrt Create_func_sqrt::s_singleton;

Item*
Create_func_sqrt::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_sqrt(arg1);
}


Create_func_str_to_date Create_func_str_to_date::s_singleton;

Item*
Create_func_str_to_date::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_str_to_date(arg1, arg2);
}


Create_func_strcmp Create_func_strcmp::s_singleton;

Item*
Create_func_strcmp::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_strcmp(arg1, arg2);
}


Create_func_substr_index Create_func_substr_index::s_singleton;

Item*
Create_func_substr_index::create(Session *session, Item *arg1, Item *arg2, Item *arg3)
{
  return new (session->mem_root) Item_func_substr_index(arg1, arg2, arg3);
}


Create_func_subtime Create_func_subtime::s_singleton;

Item*
Create_func_subtime::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_add_time(arg1, arg2, 0, 1);
}


Create_func_tan Create_func_tan::s_singleton;

Item*
Create_func_tan::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_tan(arg1);
}


Create_func_time_format Create_func_time_format::s_singleton;

Item*
Create_func_time_format::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_date_format(arg1, arg2, 1);
}


Create_func_time_to_sec Create_func_time_to_sec::s_singleton;

Item*
Create_func_time_to_sec::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_time_to_sec(arg1);
}


Create_func_timediff Create_func_timediff::s_singleton;

Item*
Create_func_timediff::create(Session *session, Item *arg1, Item *arg2)
{
  return new (session->mem_root) Item_func_timediff(arg1, arg2);
}


Create_func_to_days Create_func_to_days::s_singleton;

Item*
Create_func_to_days::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_to_days(arg1);
}


Create_func_ucase Create_func_ucase::s_singleton;

Item*
Create_func_ucase::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_ucase(arg1);
}


Create_func_unhex Create_func_unhex::s_singleton;

Item*
Create_func_unhex::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_unhex(arg1);
}


Create_func_unix_timestamp Create_func_unix_timestamp::s_singleton;

Item*
Create_func_unix_timestamp::create_native(Session *session, LEX_STRING name,
                                          List<Item> *item_list)
{
  Item *func= NULL;
  int arg_count= 0;

  if (item_list != NULL)
    arg_count= item_list->elements;

  switch (arg_count) {
  case 0:
  {
    func= new (session->mem_root) Item_func_unix_timestamp();
    break;
  }
  case 1:
  {
    Item *param_1= item_list->pop();
    func= new (session->mem_root) Item_func_unix_timestamp(param_1);
    break;
  }
  default:
  {
    my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name.str);
    break;
  }
  }

  return func;
}


Create_func_uuid Create_func_uuid::s_singleton;

Item*
Create_func_uuid::create(Session *session)
{
  return new (session->mem_root) Item_func_uuid();
}


Create_func_version Create_func_version::s_singleton;

Item*
Create_func_version::create(Session *session)
{
  return new (session->mem_root) Item_static_string_func("version()",
                                                         server_version,
                                                         (uint) strlen(server_version),
                                                         system_charset_info,
                                                         DERIVATION_SYSCONST);
}


Create_func_weekday Create_func_weekday::s_singleton;

Item*
Create_func_weekday::create(Session *session, Item *arg1)
{
  return new (session->mem_root) Item_func_weekday(arg1, 0);
}

struct Native_func_registry
{
  LEX_STRING name;
  Create_func *builder;
};

#define BUILDER(F) & F::s_singleton

/*
  MySQL native functions.
  MAINTAINER:
  - Keep sorted for human lookup. At runtime, a hash table is used.
  - keep 1 line per entry, it makes grep | sort easier
*/

static Native_func_registry func_array[] =
{
  { { C_STRING_WITH_LEN("ABS") }, BUILDER(Create_func_abs)},
  { { C_STRING_WITH_LEN("ACOS") }, BUILDER(Create_func_acos)},
  { { C_STRING_WITH_LEN("ADDTIME") }, BUILDER(Create_func_addtime)},
  { { C_STRING_WITH_LEN("ASIN") }, BUILDER(Create_func_asin)},
  { { C_STRING_WITH_LEN("ATAN") }, BUILDER(Create_func_atan)},
  { { C_STRING_WITH_LEN("ATAN2") }, BUILDER(Create_func_atan)},
  { { C_STRING_WITH_LEN("BENCHMARK") }, BUILDER(Create_func_benchmark)},
  { { C_STRING_WITH_LEN("BIN") }, BUILDER(Create_func_bin)},
  { { C_STRING_WITH_LEN("BIT_COUNT") }, BUILDER(Create_func_bit_count)},
  { { C_STRING_WITH_LEN("BIT_LENGTH") }, BUILDER(Create_func_bit_length)},
  { { C_STRING_WITH_LEN("CEIL") }, BUILDER(Create_func_ceiling)},
  { { C_STRING_WITH_LEN("CEILING") }, BUILDER(Create_func_ceiling)},
  { { C_STRING_WITH_LEN("CHARACTER_LENGTH") }, BUILDER(Create_func_char_length)},
  { { C_STRING_WITH_LEN("CHAR_LENGTH") }, BUILDER(Create_func_char_length)},
  { { C_STRING_WITH_LEN("COERCIBILITY") }, BUILDER(Create_func_coercibility)},
  { { C_STRING_WITH_LEN("CONCAT") }, BUILDER(Create_func_concat)},
  { { C_STRING_WITH_LEN("CONCAT_WS") }, BUILDER(Create_func_concat_ws)},
  { { C_STRING_WITH_LEN("CONNECTION_ID") }, BUILDER(Create_func_connection_id)},
  { { C_STRING_WITH_LEN("CONV") }, BUILDER(Create_func_conv)},
  { { C_STRING_WITH_LEN("COS") }, BUILDER(Create_func_cos)},
  { { C_STRING_WITH_LEN("COT") }, BUILDER(Create_func_cot)},
  { { C_STRING_WITH_LEN("DATEDIFF") }, BUILDER(Create_func_datediff)},
  { { C_STRING_WITH_LEN("DATE_FORMAT") }, BUILDER(Create_func_date_format)},
  { { C_STRING_WITH_LEN("DAYNAME") }, BUILDER(Create_func_dayname)},
  { { C_STRING_WITH_LEN("DAYOFMONTH") }, BUILDER(Create_func_dayofmonth)},
  { { C_STRING_WITH_LEN("DAYOFWEEK") }, BUILDER(Create_func_dayofweek)},
  { { C_STRING_WITH_LEN("DAYOFYEAR") }, BUILDER(Create_func_dayofyear)},
  { { C_STRING_WITH_LEN("DEGREES") }, BUILDER(Create_func_degrees)},
  { { C_STRING_WITH_LEN("ELT") }, BUILDER(Create_func_elt)},
  { { C_STRING_WITH_LEN("EXP") }, BUILDER(Create_func_exp)},
  { { C_STRING_WITH_LEN("EXPORT_SET") }, BUILDER(Create_func_export_set)},
  { { C_STRING_WITH_LEN("FIELD") }, BUILDER(Create_func_field)},
  { { C_STRING_WITH_LEN("FIND_IN_SET") }, BUILDER(Create_func_find_in_set)},
  { { C_STRING_WITH_LEN("FLOOR") }, BUILDER(Create_func_floor)},
  { { C_STRING_WITH_LEN("FORMAT") }, BUILDER(Create_func_format)},
  { { C_STRING_WITH_LEN("FOUND_ROWS") }, BUILDER(Create_func_found_rows)},
  { { C_STRING_WITH_LEN("FROM_DAYS") }, BUILDER(Create_func_from_days)},
  { { C_STRING_WITH_LEN("FROM_UNIXTIME") }, BUILDER(Create_func_from_unixtime)},
  { { C_STRING_WITH_LEN("GREATEST") }, BUILDER(Create_func_greatest)},
  { { C_STRING_WITH_LEN("HEX") }, BUILDER(Create_func_hex)},
  { { C_STRING_WITH_LEN("IFNULL") }, BUILDER(Create_func_ifnull)},
  { { C_STRING_WITH_LEN("INSTR") }, BUILDER(Create_func_instr)},
  { { C_STRING_WITH_LEN("ISNULL") }, BUILDER(Create_func_isnull)},
  { { C_STRING_WITH_LEN("LAST_DAY") }, BUILDER(Create_func_last_day)},
  { { C_STRING_WITH_LEN("LAST_INSERT_ID") }, BUILDER(Create_func_last_insert_id)},
  { { C_STRING_WITH_LEN("LCASE") }, BUILDER(Create_func_lcase)},
  { { C_STRING_WITH_LEN("LEAST") }, BUILDER(Create_func_least)},
  { { C_STRING_WITH_LEN("LENGTH") }, BUILDER(Create_func_length)},
  { { C_STRING_WITH_LEN("LN") }, BUILDER(Create_func_ln)},
  { { C_STRING_WITH_LEN("LOAD_FILE") }, BUILDER(Create_func_load_file)},
  { { C_STRING_WITH_LEN("LOCATE") }, BUILDER(Create_func_locate)},
  { { C_STRING_WITH_LEN("LOG") }, BUILDER(Create_func_log)},
  { { C_STRING_WITH_LEN("LOG10") }, BUILDER(Create_func_log10)},
  { { C_STRING_WITH_LEN("LOG2") }, BUILDER(Create_func_log2)},
  { { C_STRING_WITH_LEN("LOWER") }, BUILDER(Create_func_lcase)},
  { { C_STRING_WITH_LEN("LPAD") }, BUILDER(Create_func_lpad)},
  { { C_STRING_WITH_LEN("LTRIM") }, BUILDER(Create_func_ltrim)},
  { { C_STRING_WITH_LEN("MAKEDATE") }, BUILDER(Create_func_makedate)},
  { { C_STRING_WITH_LEN("MAKETIME") }, BUILDER(Create_func_maketime)},
  { { C_STRING_WITH_LEN("MAKE_SET") }, BUILDER(Create_func_make_set)},
  { { C_STRING_WITH_LEN("MONTHNAME") }, BUILDER(Create_func_monthname)},
  { { C_STRING_WITH_LEN("NULLIF") }, BUILDER(Create_func_nullif)},
  { { C_STRING_WITH_LEN("OCT") }, BUILDER(Create_func_oct)},
  { { C_STRING_WITH_LEN("OCTET_LENGTH") }, BUILDER(Create_func_length)},
  { { C_STRING_WITH_LEN("ORD") }, BUILDER(Create_func_ord)},
  { { C_STRING_WITH_LEN("PERIOD_ADD") }, BUILDER(Create_func_period_add)},
  { { C_STRING_WITH_LEN("PERIOD_DIFF") }, BUILDER(Create_func_period_diff)},
  { { C_STRING_WITH_LEN("PI") }, BUILDER(Create_func_pi)},
  { { C_STRING_WITH_LEN("POW") }, BUILDER(Create_func_pow)},
  { { C_STRING_WITH_LEN("POWER") }, BUILDER(Create_func_pow)},
  { { C_STRING_WITH_LEN("QUOTE") }, BUILDER(Create_func_quote)},
  { { C_STRING_WITH_LEN("RADIANS") }, BUILDER(Create_func_radians)},
  { { C_STRING_WITH_LEN("RAND") }, BUILDER(Create_func_rand)},
  { { C_STRING_WITH_LEN("ROUND") }, BUILDER(Create_func_round)},
  { { C_STRING_WITH_LEN("ROW_COUNT") }, BUILDER(Create_func_row_count)},
  { { C_STRING_WITH_LEN("RPAD") }, BUILDER(Create_func_rpad)},
  { { C_STRING_WITH_LEN("RTRIM") }, BUILDER(Create_func_rtrim)},
  { { C_STRING_WITH_LEN("SEC_TO_TIME") }, BUILDER(Create_func_sec_to_time)},
  { { C_STRING_WITH_LEN("SIGN") }, BUILDER(Create_func_sign)},
  { { C_STRING_WITH_LEN("SIN") }, BUILDER(Create_func_sin)},
  { { C_STRING_WITH_LEN("SPACE") }, BUILDER(Create_func_space)},
  { { C_STRING_WITH_LEN("SQRT") }, BUILDER(Create_func_sqrt)},
  { { C_STRING_WITH_LEN("STRCMP") }, BUILDER(Create_func_strcmp)},
  { { C_STRING_WITH_LEN("STR_TO_DATE") }, BUILDER(Create_func_str_to_date)},
  { { C_STRING_WITH_LEN("SUBSTRING_INDEX") }, BUILDER(Create_func_substr_index)},
  { { C_STRING_WITH_LEN("SUBTIME") }, BUILDER(Create_func_subtime)},
  { { C_STRING_WITH_LEN("TAN") }, BUILDER(Create_func_tan)},
  { { C_STRING_WITH_LEN("TIMEDIFF") }, BUILDER(Create_func_timediff)},
  { { C_STRING_WITH_LEN("TIME_FORMAT") }, BUILDER(Create_func_time_format)},
  { { C_STRING_WITH_LEN("TIME_TO_SEC") }, BUILDER(Create_func_time_to_sec)},
  { { C_STRING_WITH_LEN("TO_DAYS") }, BUILDER(Create_func_to_days)},
  { { C_STRING_WITH_LEN("UCASE") }, BUILDER(Create_func_ucase)},
  { { C_STRING_WITH_LEN("UNHEX") }, BUILDER(Create_func_unhex)},
  { { C_STRING_WITH_LEN("UNIX_TIMESTAMP") }, BUILDER(Create_func_unix_timestamp)},
  { { C_STRING_WITH_LEN("UPPER") }, BUILDER(Create_func_ucase)},
  { { C_STRING_WITH_LEN("UUID") }, BUILDER(Create_func_uuid)},
  { { C_STRING_WITH_LEN("VERSION") }, BUILDER(Create_func_version)},
  { { C_STRING_WITH_LEN("WEEKDAY") }, BUILDER(Create_func_weekday)},

  { {0, 0}, NULL}
};

static HASH native_functions_hash;

extern "C" unsigned char*
get_native_fct_hash_key(const unsigned char *buff, size_t *length,
                        bool /* unused */)
{
  Native_func_registry *func= (Native_func_registry*) buff;
  *length= func->name.length;
  return (unsigned char*) func->name.str;
}

/*
  Load the hash table for native functions.
  Note: this code is not thread safe, and is intended to be used at server
  startup only (before going multi-threaded)
*/

int item_create_init()
{
  Native_func_registry *func;

  if (hash_init(& native_functions_hash,
                system_charset_info,
                array_elements(func_array),
                0,
                0,
                (hash_get_key) get_native_fct_hash_key,
                NULL,                          /* Nothing to free */
                MYF(0)))
    return(1);

  for (func= func_array; func->builder != NULL; func++)
  {
    if (my_hash_insert(& native_functions_hash, (unsigned char*) func))
      return(1);
  }

  return(0);
}

/*
  Empty the hash table for native functions.
  Note: this code is not thread safe, and is intended to be used at server
  shutdown only (after thread requests have been executed).
*/

void item_create_cleanup()
{
  hash_free(& native_functions_hash);
  return;
}

Create_func *
find_native_function_builder(Session *,
                             LEX_STRING name)
{
  Native_func_registry *func;
  Create_func *builder= NULL;

  /* Thread safe */
  func= (Native_func_registry*) hash_search(& native_functions_hash,
                                            (unsigned char*) name.str,
                                             name.length);

  if (func)
  {
    builder= func->builder;
  }

  return builder;
}


Item*
create_func_char_cast(Session *session, Item *a, int len, const CHARSET_INFO * const cs)
{
  const CHARSET_INFO * const real_cs= (cs ? cs : session->variables.getCollation());
  return new (session->mem_root) Item_char_typecast(a, len, real_cs);
}


Item *
create_func_cast(Session *session, Item *a, Cast_target cast_type,
                 const char *c_len, const char *c_dec,
                 const CHARSET_INFO * const cs)
{
  Item *res;
  uint32_t len;
  uint32_t dec;

  switch (cast_type) {
  case ITEM_CAST_BINARY:
    res= new (session->mem_root) Item_func_binary(a);
    break;
  case ITEM_CAST_SIGNED_INT:
    res= new (session->mem_root) Item_func_signed(a);
    break;
  case ITEM_CAST_UNSIGNED_INT:
    res= new (session->mem_root) Item_func_unsigned(a);
    break;
  case ITEM_CAST_DATE:
    res= new (session->mem_root) Item_date_typecast(a);
    break;
  case ITEM_CAST_TIME:
    res= new (session->mem_root) Item_time_typecast(a);
    break;
  case ITEM_CAST_DATETIME:
    res= new (session->mem_root) Item_datetime_typecast(a);
    break;
  case ITEM_CAST_DECIMAL:
  {
    len= c_len ? atoi(c_len) : 0;
    dec= c_dec ? atoi(c_dec) : 0;
    my_decimal_trim(&len, &dec);
    if (len < dec)
    {
      my_error(ER_M_BIGGER_THAN_D, MYF(0), "");
      return 0;
    }
    if (len > DECIMAL_MAX_PRECISION)
    {
      my_error(ER_TOO_BIG_PRECISION, MYF(0), len, a->name,
               DECIMAL_MAX_PRECISION);
      return 0;
    }
    if (dec > DECIMAL_MAX_SCALE)
    {
      my_error(ER_TOO_BIG_SCALE, MYF(0), dec, a->name,
               DECIMAL_MAX_SCALE);
      return 0;
    }
    res= new (session->mem_root) Item_decimal_typecast(a, len, dec);
    break;
  }
  case ITEM_CAST_CHAR:
  {
    len= c_len ? atoi(c_len) : -1;
    res= create_func_char_cast(session, a, len, cs);
    break;
  }
  default:
  {
    assert(0);
    res= 0;
    break;
  }
  }
  return res;
}
