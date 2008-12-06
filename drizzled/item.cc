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
#include CMATH_H

#include <drizzled/sql_select.h>
#include <drizzled/error.h>
#include <drizzled/show.h>
#include <drizzled/item/cmpfunc.h>
#include <drizzled/item/cache_row.h>
#include <drizzled/item/type_holder.h>
#include <drizzled/item/sum.h>
#include <drizzled/functions/str/conv_charset.h>
#include <drizzled/virtual_column_info.h>
#include <drizzled/sql_base.h>


#include <drizzled/field/str.h>
#include <drizzled/field/longstr.h>
#include <drizzled/field/num.h>
#include <drizzled/field/blob.h>
#include <drizzled/field/enum.h>
#include <drizzled/field/null.h>
#include <drizzled/field/date.h>
#include <drizzled/field/fdecimal.h>
#include <drizzled/field/real.h>
#include <drizzled/field/double.h>
#include <drizzled/field/long.h>
#include <drizzled/field/int64_t.h>
#include <drizzled/field/num.h>
#include <drizzled/field/timetype.h>
#include <drizzled/field/timestamp.h>
#include <drizzled/field/datetime.h>
#include <drizzled/field/varstring.h>


#if defined(CMATH_NAMESPACE)
using namespace CMATH_NAMESPACE;
#endif

const String my_null_string("NULL", 4, default_charset_info);

/*****************************************************************************
** Item functions
*****************************************************************************/

/**
  Init all special items.
*/

void item_init(void)
{
}


bool Item::is_expensive_processor(unsigned char *)
{
  return 0;
}

void Item::fix_after_pullout(st_select_lex *, Item **)
{}


Field *Item::tmp_table_field(Table *)
{
  return 0;
}


const char *Item::full_name(void) const
{
  return name ? name : "???";
}


int64_t Item::val_int_endpoint(bool, bool *)
{
  assert(0);
  return 0;
}


/**
  @todo
    Make this functions class dependent
*/

bool Item::val_bool()
{
  switch(result_type()) {
  case INT_RESULT:
    return val_int() != 0;
  case DECIMAL_RESULT:
  {
    my_decimal decimal_value;
    my_decimal *val= val_decimal(&decimal_value);
    if (val)
      return !my_decimal_is_zero(val);
    return 0;
  }
  case REAL_RESULT:
  case STRING_RESULT:
    return val_real() != 0.0;
  case ROW_RESULT:
  default:
    assert(0);
    return 0;                                   // Wrong (but safe)
  }
}


String *Item::val_string_from_real(String *str)
{
  double nr= val_real();
  if (null_value)
    return 0;					/* purecov: inspected */
  str->set_real(nr,decimals, &my_charset_bin);
  return str;
}


String *Item::val_string_from_int(String *str)
{
  int64_t nr= val_int();
  if (null_value)
    return 0;
  str->set_int(nr, unsigned_flag, &my_charset_bin);
  return str;
}


String *Item::val_string_from_decimal(String *str)
{
  my_decimal dec_buf, *dec= val_decimal(&dec_buf);
  if (null_value)
    return 0;
  my_decimal_round(E_DEC_FATAL_ERROR, dec, decimals, false, &dec_buf);
  my_decimal2string(E_DEC_FATAL_ERROR, &dec_buf, 0, 0, 0, str);
  return str;
}


my_decimal *Item::val_decimal_from_real(my_decimal *decimal_value)
{
  double nr= val_real();
  if (null_value)
    return 0;
  double2my_decimal(E_DEC_FATAL_ERROR, nr, decimal_value);
  return (decimal_value);
}


my_decimal *Item::val_decimal_from_int(my_decimal *decimal_value)
{
  int64_t nr= val_int();
  if (null_value)
    return 0;
  int2my_decimal(E_DEC_FATAL_ERROR, nr, unsigned_flag, decimal_value);
  return decimal_value;
}


my_decimal *Item::val_decimal_from_string(my_decimal *decimal_value)
{
  String *res;
  char *end_ptr;
  if (!(res= val_str(&str_value)))
    return 0;                                   // NULL or EOM

  end_ptr= (char*) res->ptr()+ res->length();
  if (str2my_decimal(E_DEC_FATAL_ERROR & ~E_DEC_BAD_NUM,
                     res->ptr(), res->length(), res->charset(),
                     decimal_value) & E_DEC_BAD_NUM)
  {
    push_warning_printf(current_session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                        ER_TRUNCATED_WRONG_VALUE,
                        ER(ER_TRUNCATED_WRONG_VALUE), "DECIMAL",
                        str_value.c_ptr());
  }
  return decimal_value;
}


my_decimal *Item::val_decimal_from_date(my_decimal *decimal_value)
{
  assert(fixed == 1);
  DRIZZLE_TIME ltime;
  if (get_date(&ltime, TIME_FUZZY_DATE))
  {
    my_decimal_set_zero(decimal_value);
    null_value= 1;                               // set NULL, stop processing
    return 0;
  }
  return date2my_decimal(&ltime, decimal_value);
}


my_decimal *Item::val_decimal_from_time(my_decimal *decimal_value)
{
  assert(fixed == 1);
  DRIZZLE_TIME ltime;
  if (get_time(&ltime))
  {
    my_decimal_set_zero(decimal_value);
    return 0;
  }
  return date2my_decimal(&ltime, decimal_value);
}


double Item::val_real_from_decimal()
{
  /* Note that fix_fields may not be called for Item_avg_field items */
  double result;
  my_decimal value_buff, *dec_val= val_decimal(&value_buff);
  if (null_value)
    return 0.0;
  my_decimal2double(E_DEC_FATAL_ERROR, dec_val, &result);
  return result;
}


int64_t Item::val_int_from_decimal()
{
  /* Note that fix_fields may not be called for Item_avg_field items */
  int64_t result;
  my_decimal value, *dec_val= val_decimal(&value);
  if (null_value)
    return 0;
  my_decimal2int(E_DEC_FATAL_ERROR, dec_val, unsigned_flag, &result);
  return result;
}

int Item::save_time_in_field(Field *field)
{
  DRIZZLE_TIME ltime;
  if (get_time(&ltime))
    return set_field_to_null(field);
  field->set_notnull();
  return field->store_time(&ltime, DRIZZLE_TIMESTAMP_TIME);
}


int Item::save_date_in_field(Field *field)
{
  DRIZZLE_TIME ltime;
  if (get_date(&ltime, TIME_FUZZY_DATE))
    return set_field_to_null(field);
  field->set_notnull();
  return field->store_time(&ltime, DRIZZLE_TIMESTAMP_DATETIME);
}


/*
  Store the string value in field directly

  SYNOPSIS
    Item::save_str_value_in_field()
    field   a pointer to field where to store
    result  the pointer to the string value to be stored

  DESCRIPTION
    The method is used by Item_*::save_in_field implementations
    when we don't need to calculate the value to store
    See Item_string::save_in_field() implementation for example

  IMPLEMENTATION
    Check if the Item is null and stores the NULL or the
    result value in the field accordingly.

  RETURN
    Nonzero value if error
*/

int Item::save_str_value_in_field(Field *field, String *result)
{
  if (null_value)
    return set_field_to_null(field);
  field->set_notnull();
  return field->store(result->ptr(), result->length(),
		      collation.collation);
}


Item::Item():
  is_expensive_cache(-1), name(0), orig_name(0), name_length(0),
  fixed(0), is_autogenerated_name(true),
  collation(&my_charset_bin, DERIVATION_COERCIBLE)
{
  marker= 0;
  maybe_null= false;
  null_value= false;
  with_sum_func= false;
  unsigned_flag= false;
  decimals= 0;
  max_length= 0;
  with_subselect= 0;
  cmp_context= (Item_result)-1;

  /* Put item in free list so that we can free all items at end */
  Session *session= current_session;
  next= session->free_list;
  session->free_list= this;
  /*
    Item constructor can be called during execution other then SQL_COM
    command => we should check session->lex->current_select on zero (session->lex
    can be uninitialised)
  */
  if (session->lex->current_select)
  {
    enum_parsing_place place=
      session->lex->current_select->parsing_place;
    if (place == SELECT_LIST ||
	place == IN_HAVING)
      session->lex->current_select->select_n_having_items++;
  }
}

/**
  Constructor used by Item_field, Item_ref & aggregate (sum)
  functions.

  Used for duplicating lists in processing queries with temporary
  tables.
*/
Item::Item(Session *session, Item *item):
  is_expensive_cache(-1),
  str_value(item->str_value),
  name(item->name),
  orig_name(item->orig_name),
  max_length(item->max_length),
  marker(item->marker),
  decimals(item->decimals),
  maybe_null(item->maybe_null),
  null_value(item->null_value),
  unsigned_flag(item->unsigned_flag),
  with_sum_func(item->with_sum_func),
  fixed(item->fixed),
  collation(item->collation),
  cmp_context(item->cmp_context)
{
  next= session->free_list;				// Put in free list
  session->free_list= this;
}


uint32_t Item::decimal_precision() const
{
  Item_result restype= result_type();

  if ((restype == DECIMAL_RESULT) || (restype == INT_RESULT))
    return cmin(my_decimal_length_to_precision(max_length, decimals, unsigned_flag),
               (unsigned int)DECIMAL_MAX_PRECISION);
  return cmin(max_length, (uint32_t)DECIMAL_MAX_PRECISION);
}


int Item::decimal_int_part() const
{
  return my_decimal_int_part(decimal_precision(), decimals);
}


void Item::print(String *str, enum_query_type)
{
  str->append(full_name());
}


void Item::print_item_w_name(String *str, enum_query_type query_type)
{
  print(str, query_type);

  if (name)
  {
    Session *session= current_session;
    str->append(STRING_WITH_LEN(" AS "));
    append_identifier(session, str, name, (uint) strlen(name));
  }
}


void Item::split_sum_func(Session *, Item **, List<Item> &)
{}


void Item::cleanup()
{
  fixed=0;
  marker= 0;
  if (orig_name)
    name= orig_name;
  return;
}


/**
  cleanup() item if it is 'fixed'.

  @param arg   a dummy parameter, is not used here
*/

bool Item::cleanup_processor(unsigned char *)
{
  if (fixed)
    cleanup();
  return false;
}


/**
  rename item (used for views, cleanup() return original name).

  @param new_name	new name of item;
*/

void Item::rename(char *new_name)
{
  /*
    we can compare pointers to names here, because if name was not changed,
    pointer will be same
  */
  if (!orig_name && new_name != name)
    orig_name= name;
  name= new_name;
}


/**
  Traverse item tree possibly transforming it (replacing items).

  This function is designed to ease transformation of Item trees.
  Re-execution note: every such transformation is registered for
  rollback by Session::change_item_tree() and is rolled back at the end
  of execution by Session::rollback_item_tree_changes().

  Therefore:
  - this function can not be used at prepared statement prepare
  (in particular, in fix_fields!), as only permanent
  transformation of Item trees are allowed at prepare.
  - the transformer function shall allocate new Items in execution
  memory root (session->mem_root) and not anywhere else: allocated
  items will be gone in the end of execution.

  If you don't need to transform an item tree, but only traverse
  it, please use Item::walk() instead.


  @param transformer    functor that performs transformation of a subtree
  @param arg            opaque argument passed to the functor

  @return
    Returns pointer to the new subtree root.  Session::change_item_tree()
    should be called for it if transformation took place, i.e. if a
    pointer to newly allocated item is returned.
*/

Item* Item::transform(Item_transformer transformer, unsigned char *arg)
{
  return (this->*transformer)(arg);
}


bool Item::check_cols(uint32_t c)
{
  if (c != 1)
  {
    my_error(ER_OPERAND_COLUMNS, MYF(0), c);
    return 1;
  }
  return 0;
}


void Item::set_name(const char *str, uint32_t length, const CHARSET_INFO * const cs)
{
  if (!length)
  {
    /* Empty string, used by AS or internal function like last_insert_id() */
    name= (char*) str;
    name_length= 0;
    return;
  }
  if (cs->ctype)
  {
    uint32_t orig_len= length;
    /*
      This will probably need a better implementation in the future:
      a function in CHARSET_INFO structure.
    */
    while (length && !my_isgraph(cs,*str))
    {						// Fix problem with yacc
      length--;
      str++;
    }
    if (orig_len != length && !is_autogenerated_name)
    {
      if (length == 0)
        push_warning_printf(current_session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                            ER_NAME_BECOMES_EMPTY, ER(ER_NAME_BECOMES_EMPTY),
                            str + length - orig_len);
      else
        push_warning_printf(current_session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                            ER_REMOVED_SPACES, ER(ER_REMOVED_SPACES),
                            str + length - orig_len);
    }
  }
  if (!my_charset_same(cs, system_charset_info))
  {
    size_t res_length;
    name= sql_strmake_with_convert(str, name_length= length, cs,
				   MAX_ALIAS_NAME, system_charset_info,
				   &res_length);
  }
  else
    name= sql_strmake(str, (name_length= cmin(length,(unsigned int)MAX_ALIAS_NAME)));
}


/**
  @details
  This function is called when:
  - Comparing items in the WHERE clause (when doing where optimization)
  - When trying to find an order_st BY/GROUP BY item in the SELECT part
*/

bool Item::eq(const Item *item, bool) const
{
  /*
    Note, that this is never true if item is a Item_param:
    for all basic constants we have special checks, and Item_param's
    type() can be only among basic constant types.
  */
  return type() == item->type() && name && item->name &&
    !my_strcasecmp(system_charset_info,name,item->name);
}


Item *Item::safe_charset_converter(const CHARSET_INFO * const tocs)
{
  Item_func_conv_charset *conv= new Item_func_conv_charset(this, tocs, 1);
  return conv->safe ? conv : NULL;
}


Item *Item_static_float_func::safe_charset_converter(const CHARSET_INFO * const)
{
  Item_string *conv;
  char buf[64];
  String *s, tmp(buf, sizeof(buf), &my_charset_bin);
  s= val_str(&tmp);
  if ((conv= new Item_static_string_func(func_name, s->ptr(), s->length(),
                                         s->charset())))
  {
    conv->str_value.copy();
    conv->str_value.mark_as_const();
  }
  return conv;
}


Item *Item_string::safe_charset_converter(const CHARSET_INFO * const tocs)
{
  Item_string *conv;
  uint32_t conv_errors;
  char *ptr;
  String tmp, cstr, *ostr= val_str(&tmp);
  cstr.copy(ostr->ptr(), ostr->length(), ostr->charset(), tocs, &conv_errors);
  if (conv_errors || !(conv= new Item_string(cstr.ptr(), cstr.length(),
                                             cstr.charset(),
                                             collation.derivation)))
  {
    /*
      Safe conversion is not possible (or EOM).
      We could not convert a string into the requested character set
      without data loss. The target charset does not cover all the
      characters from the string. Operation cannot be done correctly.
    */
    return NULL;
  }
  if (!(ptr= current_session->strmake(cstr.ptr(), cstr.length())))
    return NULL;
  conv->str_value.set(ptr, cstr.length(), cstr.charset());
  /* Ensure that no one is going to change the result string */
  conv->str_value.mark_as_const();
  return conv;
}


Item *Item_param::safe_charset_converter(const CHARSET_INFO * const tocs)
{
  if (const_item())
  {
    uint32_t cnv_errors;
    String *ostr= val_str(&cnvstr);
    cnvitem->str_value.copy(ostr->ptr(), ostr->length(),
                            ostr->charset(), tocs, &cnv_errors);
    if (cnv_errors)
       return NULL;
    cnvitem->str_value.mark_as_const();
    cnvitem->max_length= cnvitem->str_value.numchars() * tocs->mbmaxlen;
    return cnvitem;
  }
  return NULL;
}


Item *Item_static_string_func::safe_charset_converter(const CHARSET_INFO * const tocs)
{
  Item_string *conv;
  uint32_t conv_errors;
  String tmp, cstr, *ostr= val_str(&tmp);
  cstr.copy(ostr->ptr(), ostr->length(), ostr->charset(), tocs, &conv_errors);
  if (conv_errors ||
      !(conv= new Item_static_string_func(func_name,
                                          cstr.ptr(), cstr.length(),
                                          cstr.charset(),
                                          collation.derivation)))
  {
    /*
      Safe conversion is not possible (or EOM).
      We could not convert a string into the requested character set
      without data loss. The target charset does not cover all the
      characters from the string. Operation cannot be done correctly.
    */
    return NULL;
  }
  conv->str_value.copy();
  /* Ensure that no one is going to change the result string */
  conv->str_value.mark_as_const();
  return conv;
}


bool Item_string::eq(const Item *item, bool binary_cmp) const
{
  if (type() == item->type() && item->basic_const_item())
  {
    if (binary_cmp)
      return !stringcmp(&str_value, &item->str_value);
    return (collation.collation == item->collation.collation &&
	    !sortcmp(&str_value, &item->str_value, collation.collation));
  }
  return 0;
}


/**
  Get the value of the function as a DRIZZLE_TIME structure.
  As a extra convenience the time structure is reset on error!
*/

bool Item::get_date(DRIZZLE_TIME *ltime,uint32_t fuzzydate)
{
  if (result_type() == STRING_RESULT)
  {
    char buff[40];
    String tmp(buff,sizeof(buff), &my_charset_bin),*res;
    if (!(res=val_str(&tmp)) ||
        str_to_datetime_with_warn(res->ptr(), res->length(),
                                  ltime, fuzzydate) <= DRIZZLE_TIMESTAMP_ERROR)
      goto err;
  }
  else
  {
    int64_t value= val_int();
    int was_cut;
    if (number_to_datetime(value, ltime, fuzzydate, &was_cut) == -1L)
    {
      char buff[22], *end;
      end= int64_t10_to_str(value, buff, -10);
      make_truncated_value_warning(current_session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                                   buff, (int) (end-buff), DRIZZLE_TIMESTAMP_NONE,
                                   NULL);
      goto err;
    }
  }
  return 0;

err:
  memset(ltime, 0, sizeof(*ltime));
  return 1;
}

/**
  Get time of first argument.\

  As a extra convenience the time structure is reset on error!
*/

bool Item::get_time(DRIZZLE_TIME *ltime)
{
  char buff[40];
  String tmp(buff,sizeof(buff),&my_charset_bin),*res;
  if (!(res=val_str(&tmp)) ||
      str_to_time_with_warn(res->ptr(), res->length(), ltime))
  {
    memset(ltime, 0, sizeof(*ltime));
    return true;
  }
  return false;
}


bool Item::get_date_result(DRIZZLE_TIME *ltime,uint32_t fuzzydate)
{
  return get_date(ltime,fuzzydate);
}


bool Item::is_null()
{
  return false;
}


void Item::update_null_value ()
{
  (void) val_int();
}


void Item::top_level_item(void)
{}


void Item::set_result_field(Field *)
{}


bool Item::is_result_field(void)
{
  return 0;
}


bool Item::is_bool_func(void)
{
  return 0;
}


void Item::save_in_result_field(bool)
{}


void Item::no_rows_in_result(void)
{}


Item *Item::copy_or_same(Session *)
{
  return this;
}


Item *Item::copy_andor_structure(Session *)
{
  return this;
}


Item *Item::real_item(void)
{
  return this;
}


Item *Item::get_tmp_table_item(Session *session)
{
  return copy_or_same(session);
}


const CHARSET_INFO *Item::default_charset()
{
  return current_session->variables.collation_connection;
}


const CHARSET_INFO *Item::compare_collation()
{
  return NULL;
}


bool Item::walk(Item_processor processor, bool, unsigned char *arg)
{
  return (this->*processor)(arg);
}


Item* Item::compile(Item_analyzer analyzer, unsigned char **arg_p,
                    Item_transformer transformer, unsigned char *arg_t)
{
  if ((this->*analyzer) (arg_p))
    return ((this->*transformer) (arg_t));
  return 0;
}


void Item::traverse_cond(Cond_traverser traverser, void *arg, traverse_order)
{
  (*traverser)(this, arg);
}


bool Item::remove_dependence_processor(unsigned char *)
{
  return 0;
}


bool Item::remove_fixed(unsigned char *)
{
  fixed= 0;
  return 0;
}


bool Item::collect_item_field_processor(unsigned char *)
{
  return 0;
}


bool Item::find_item_in_field_list_processor(unsigned char *)
{
  return 0;
}


bool Item::change_context_processor(unsigned char *)
{
  return 0;
}

bool Item::reset_query_id_processor(unsigned char *)
{
  return 0;
}


bool Item::register_field_in_read_map(unsigned char *)
{
  return 0;
}


bool Item::register_field_in_bitmap(unsigned char *)
{
  return 0;
}


bool Item::subst_argument_checker(unsigned char **arg)
{
  if (*arg)
    *arg= NULL;
  return true;
}


bool Item::check_vcol_func_processor(unsigned char *)
{
  return true;
}


Item *Item::equal_fields_propagator(unsigned char *)
{
  return this;
}


bool Item::set_no_const_sub(unsigned char *)
{
  return false;
}


Item *Item::replace_equal_field(unsigned char *)
{
  return this;
}


uint32_t Item::cols()
{
  return 1;
}


Item* Item::element_index(uint32_t)
{
  return this;
}


Item** Item::addr(uint32_t)
{
  return 0;
}


bool Item::null_inside()
{
  return 0;
}


void Item::bring_value()
{}


Item *Item::neg_transformer(Session *)
{
  return NULL;
}


Item *Item::update_value_transformer(unsigned char *)
{
  return this;
}


void Item::delete_self()
{
  cleanup();
  delete this;
}

bool Item::result_as_int64_t()
{
  return false;
}


bool Item::is_expensive()
{
  if (is_expensive_cache < 0)
    is_expensive_cache= walk(&Item::is_expensive_processor, 0,
                             (unsigned char*)0);
  return test(is_expensive_cache);
}


int Item::save_in_field_no_warnings(Field *field, bool no_conversions)
{
  int res;
  Table *table= field->table;
  Session *session= table->in_use;
  enum_check_fields tmp= session->count_cuted_fields;
  ulong sql_mode= session->variables.sql_mode;
  session->variables.sql_mode&= ~(MODE_NO_ZERO_DATE);
  session->count_cuted_fields= CHECK_FIELD_IGNORE;
  res= save_in_field(field, no_conversions);
  session->count_cuted_fields= tmp;
  session->variables.sql_mode= sql_mode;
  return res;
}


/*
 need a special class to adjust printing : references to aggregate functions
 must not be printed as refs because the aggregate functions that are added to
 the front of select list are not printed as well.
*/
class Item_aggregate_ref : public Item_ref
{
public:
  Item_aggregate_ref(Name_resolution_context *context_arg, Item **item,
                  const char *table_name_arg, const char *field_name_arg)
    :Item_ref(context_arg, item, table_name_arg, field_name_arg) {}

  virtual inline void print (String *str, enum_query_type query_type)
  {
    if (ref)
      (*ref)->print(str, query_type);
    else
      Item_ident::print(str, query_type);
  }
};


/**
  Move SUM items out from item tree and replace with reference.

  @param session			Thread handler
  @param ref_pointer_array	Pointer to array of reference fields
  @param fields		All fields in select
  @param ref			Pointer to item
  @param skip_registered       <=> function be must skipped for registered
                               SUM items

  @note
    This is from split_sum_func() for items that should be split

    All found SUM items are added FIRST in the fields list and
    we replace the item with a reference.

    session->fatal_error() may be called if we are out of memory
*/

void Item::split_sum_func(Session *session, Item **ref_pointer_array,
                          List<Item> &fields, Item **ref,
                          bool skip_registered)
{
  /* An item of type Item_sum  is registered <=> ref_by != 0 */
  if (type() == SUM_FUNC_ITEM && skip_registered &&
      ((Item_sum *) this)->ref_by)
    return;
  if ((type() != SUM_FUNC_ITEM && with_sum_func) ||
      (type() == FUNC_ITEM &&
       (((Item_func *) this)->functype() == Item_func::ISNOTNULLTEST_FUNC ||
        ((Item_func *) this)->functype() == Item_func::TRIG_COND_FUNC)))
  {
    /* Will split complicated items and ignore simple ones */
    split_sum_func(session, ref_pointer_array, fields);
  }
  else if ((type() == SUM_FUNC_ITEM || (used_tables() & ~PARAM_TABLE_BIT)) &&
           type() != SUBSELECT_ITEM &&
           type() != REF_ITEM)
  {
    /*
      Replace item with a reference so that we can easily calculate
      it (in case of sum functions) or copy it (in case of fields)

      The test above is to ensure we don't do a reference for things
      that are constants (PARAM_TABLE_BIT is in effect a constant)
      or already referenced (for example an item in HAVING)
      Exception is Item_direct_view_ref which we need to convert to
      Item_ref to allow fields from view being stored in tmp table.
    */
    Item_aggregate_ref *item_ref;
    uint32_t el= fields.elements;
    Item *real_itm= real_item();

    ref_pointer_array[el]= real_itm;
    if (!(item_ref= new Item_aggregate_ref(&session->lex->current_select->context,
                                           ref_pointer_array + el, 0, name)))
      return;                                   // fatal_error is set
    if (type() == SUM_FUNC_ITEM)
      item_ref->depended_from= ((Item_sum *) this)->depended_from();
    fields.push_front(real_itm);
    session->change_item_tree(ref, item_ref);
  }
}


/**
  Create an item from a string we KNOW points to a valid int64_t
  end \\0 terminated number string.
  This is always 'signed'. Unsigned values are created with Item_uint()
*/

Item_int::Item_int(const char *str_arg, uint32_t length)
{
  char *end_ptr= (char*) str_arg + length;
  int error;
  value= my_strtoll10(str_arg, &end_ptr, &error);
  max_length= (uint) (end_ptr - str_arg);
  name= (char*) str_arg;
  fixed= 1;
}


my_decimal *Item_int::val_decimal(my_decimal *decimal_value)
{
  int2my_decimal(E_DEC_FATAL_ERROR, value, unsigned_flag, decimal_value);
  return decimal_value;
}

String *Item_int::val_str(String *str)
{
  // following assert is redundant, because fixed=1 assigned in constructor
  assert(fixed == 1);
  str->set(value, &my_charset_bin);
  return str;
}

void Item_int::print(String *str, enum_query_type)
{
  // my_charset_bin is good enough for numbers
  str_value.set(value, &my_charset_bin);
  str->append(str_value);
}


Item_uint::Item_uint(const char *str_arg, uint32_t length):
  Item_int(str_arg, length)
{
  unsigned_flag= 1;
}


Item_uint::Item_uint(const char *str_arg, int64_t i, uint32_t length):
  Item_int(str_arg, i, length)
{
  unsigned_flag= 1;
}


String *Item_uint::val_str(String *str)
{
  // following assert is redundant, because fixed=1 assigned in constructor
  assert(fixed == 1);
  str->set((uint64_t) value, &my_charset_bin);
  return str;
}


void Item_uint::print(String *str, enum_query_type)
{
  // latin1 is good enough for numbers
  str_value.set((uint64_t) value, default_charset());
  str->append(str_value);
}


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


String *Item_float::val_str(String *str)
{
  // following assert is redundant, because fixed=1 assigned in constructor
  assert(fixed == 1);
  str->set_real(value,decimals,&my_charset_bin);
  return str;
}


int64_t Item_float::val_int()
{
  assert(fixed == 1);
  if (value <= (double) INT64_MIN)
  {
     return INT64_MIN;
  }
  else if (value >= (double) (uint64_t) INT64_MAX)
  {
    return INT64_MAX;
  }
  return (int64_t) rint(value);
}

my_decimal *Item_float::val_decimal(my_decimal *decimal_value)
{
  // following assert is redundant, because fixed=1 assigned in constructor
  assert(fixed == 1);
  double2my_decimal(E_DEC_FATAL_ERROR, value, decimal_value);
  return (decimal_value);
}


void Item_string::print(String *str, enum_query_type query_type)
{
  if (query_type == QT_ORDINARY && is_cs_specified())
  {
    str->append('_');
    str->append(collation.collation->csname);
  }

  str->append('\'');

  if (query_type == QT_ORDINARY ||
      my_charset_same(str_value.charset(), system_charset_info))
  {
    str_value.print(str);
  }
  else
  {
    Session *session= current_session;
    LEX_STRING utf8_lex_str;

    session->convert_string(&utf8_lex_str,
                        system_charset_info,
                        str_value.c_ptr_safe(),
                        str_value.length(),
                        str_value.charset());

    String utf8_str(utf8_lex_str.str,
                    utf8_lex_str.length,
                    system_charset_info);

    utf8_str.print(str);
  }

  str->append('\'');
}


double Item_string::val_real()
{
  assert(fixed == 1);
  int error;
  char *end, *org_end;
  double tmp;
  const CHARSET_INFO * const cs= str_value.charset();

  org_end= (char*) str_value.ptr() + str_value.length();
  tmp= my_strntod(cs, (char*) str_value.ptr(), str_value.length(), &end,
                  &error);
  if (error || (end != org_end && !check_if_only_end_space(cs, end, org_end)))
  {
    /*
      We can use str_value.ptr() here as Item_string is gurantee to put an
      end \0 here.
    */
    push_warning_printf(current_session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                        ER_TRUNCATED_WRONG_VALUE,
                        ER(ER_TRUNCATED_WRONG_VALUE), "DOUBLE",
                        str_value.ptr());
  }
  return tmp;
}


/**
  @todo
  Give error if we wanted a signed integer and we got an unsigned one
*/
int64_t Item_string::val_int()
{
  assert(fixed == 1);
  int err;
  int64_t tmp;
  char *end= (char*) str_value.ptr()+ str_value.length();
  char *org_end= end;
  const CHARSET_INFO * const cs= str_value.charset();

  tmp= (*(cs->cset->strtoll10))(cs, str_value.ptr(), &end, &err);
  /*
    TODO: Give error if we wanted a signed integer and we got an unsigned
    one
  */
  if (err > 0 ||
      (end != org_end && !check_if_only_end_space(cs, end, org_end)))
  {
    push_warning_printf(current_session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                        ER_TRUNCATED_WRONG_VALUE,
                        ER(ER_TRUNCATED_WRONG_VALUE), "INTEGER",
                        str_value.ptr());
  }
  return tmp;
}


my_decimal *Item_string::val_decimal(my_decimal *decimal_value)
{
  return val_decimal_from_string(decimal_value);
}


bool Item_null::eq(const Item *item, bool) const
{ return item->type() == type(); }


double Item_null::val_real()
{
  // following assert is redundant, because fixed=1 assigned in constructor
  assert(fixed == 1);
  null_value=1;
  return 0.0;
}
int64_t Item_null::val_int()
{
  // following assert is redundant, because fixed=1 assigned in constructor
  assert(fixed == 1);
  null_value=1;
  return 0;
}
/* ARGSUSED */
String *Item_null::val_str(String *)
{
  // following assert is redundant, because fixed=1 assigned in constructor
  assert(fixed == 1);
  null_value=1;
  return 0;
}

my_decimal *Item_null::val_decimal(my_decimal *)
{
  return 0;
}


Item *Item_null::safe_charset_converter(const CHARSET_INFO * const tocs)
{
  collation.set(tocs);
  return this;
}

/*********************** Item_param related ******************************/

/**
  Default function of Item_param::set_param_func, so in case
  of malformed packet the server won't SIGSEGV.
*/

static void
default_set_param_func(Item_param *param, unsigned char **, ulong)
{
  param->set_null();
}


Item_param::Item_param(uint32_t pos_in_query_arg) :
  state(NO_VALUE),
  item_result_type(STRING_RESULT),
  /* Don't pretend to be a literal unless value for this item is set. */
  item_type(PARAM_ITEM),
  param_type(DRIZZLE_TYPE_VARCHAR),
  pos_in_query(pos_in_query_arg),
  set_param_func(default_set_param_func),
  limit_clause_param(false)
{
  name= (char*) "?";
  /*
    Since we can't say whenever this item can be NULL or cannot be NULL
    before mysql_stmt_execute(), so we assuming that it can be NULL until
    value is set.
  */
  maybe_null= 1;
  cnvitem= new Item_string("", 0, &my_charset_bin, DERIVATION_COERCIBLE);
  cnvstr.set(cnvbuf, sizeof(cnvbuf), &my_charset_bin);
}


void Item_param::set_null()
{
  /* These are cleared after each execution by reset() method */
  null_value= 1;
  /*
    Because of NULL and string values we need to set max_length for each new
    placeholder value: user can submit NULL for any placeholder type, and
    string length can be different in each execution.
  */
  max_length= 0;
  decimals= 0;
  state= NULL_VALUE;
  item_type= Item::NULL_ITEM;
  return;
}

void Item_param::set_int(int64_t i, uint32_t max_length_arg)
{
  value.integer= (int64_t) i;
  state= INT_VALUE;
  max_length= max_length_arg;
  decimals= 0;
  maybe_null= 0;
  return;
}

void Item_param::set_double(double d)
{
  value.real= d;
  state= REAL_VALUE;
  max_length= DBL_DIG + 8;
  decimals= NOT_FIXED_DEC;
  maybe_null= 0;
  return;
}


/**
  Set decimal parameter value from string.

  @param str      character string
  @param length   string length

  @note
    As we use character strings to send decimal values in
    binary protocol, we use str2my_decimal to convert it to
    internal decimal value.
*/

void Item_param::set_decimal(char *str, ulong length)
{
  char *end;

  end= str+length;
  str2my_decimal((uint)E_DEC_FATAL_ERROR, str, &decimal_value, &end);
  state= DECIMAL_VALUE;
  decimals= decimal_value.frac;
  max_length= my_decimal_precision_to_length(decimal_value.precision(),
                                             decimals, unsigned_flag);
  maybe_null= 0;
  return;
}


/**
  Set parameter value from DRIZZLE_TIME value.

  @param tm              datetime value to set (time_type is ignored)
  @param type            type of datetime value
  @param max_length_arg  max length of datetime value as string

  @note
    If we value to be stored is not normalized, zero value will be stored
    instead and proper warning will be produced. This function relies on
    the fact that even wrong value sent over binary protocol fits into
    MAX_DATE_STRING_REP_LENGTH buffer.
*/
void Item_param::set_time(DRIZZLE_TIME *tm,
                          enum enum_drizzle_timestamp_type time_type,
                          uint32_t max_length_arg)
{
  value.time= *tm;
  value.time.time_type= time_type;

  if (value.time.year > 9999 || value.time.month > 12 ||
      value.time.day > 31 ||
      ((time_type != DRIZZLE_TIMESTAMP_TIME) && value.time.hour > 23) ||
      value.time.minute > 59 || value.time.second > 59)
  {
    char buff[MAX_DATE_STRING_REP_LENGTH];
    uint32_t length= my_TIME_to_str(&value.time, buff);
    make_truncated_value_warning(current_session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                                 buff, length, time_type, 0);
    set_zero_time(&value.time, DRIZZLE_TIMESTAMP_ERROR);
  }

  state= TIME_VALUE;
  maybe_null= 0;
  max_length= max_length_arg;
  decimals= 0;
  return;
}


bool Item_param::set_str(const char *str, ulong length)
{
  /*
    Assign string with no conversion: data is converted only after it's
    been written to the binary log.
  */
  uint32_t dummy_errors;
  if (str_value.copy(str, length, &my_charset_bin, &my_charset_bin,
                     &dummy_errors))
    return(true);
  state= STRING_VALUE;
  max_length= length;
  maybe_null= 0;
  /* max_length and decimals are set after charset conversion */
  /* sic: str may be not null-terminated */
  return(false);
}


bool Item_param::set_longdata(const char *str, ulong length)
{
  /*
    If client character set is multibyte, end of long data packet
    may hit at the middle of a multibyte character.  Additionally,
    if binary log is open we must write long data value to the
    binary log in character set of client. This is why we can't
    convert long data to connection character set as it comes
    (here), and first have to concatenate all pieces together,
    write query to the binary log and only then perform conversion.
  */
  if (str_value.append(str, length, &my_charset_bin))
    return(true);
  state= LONG_DATA_VALUE;
  maybe_null= 0;

  return(false);
}


/**
  Set parameter value from user variable value.

  @param session   Current thread
  @param entry User variable structure (NULL means use NULL value)

  @retval
    0 OK
  @retval
    1 Out of memory
*/

bool Item_param::set_from_user_var(Session *session, const user_var_entry *entry)
{
  if (entry && entry->value)
  {
    item_result_type= entry->type;
    unsigned_flag= entry->unsigned_flag;
    if (limit_clause_param)
    {
      bool unused;
      set_int(entry->val_int(&unused), MY_INT64_NUM_DECIMAL_DIGITS);
      item_type= Item::INT_ITEM;
      return(!unsigned_flag && value.integer < 0 ? 1 : 0);
    }
    switch (item_result_type) {
    case REAL_RESULT:
      set_double(*(double*)entry->value);
      item_type= Item::REAL_ITEM;
      break;
    case INT_RESULT:
      set_int(*(int64_t*)entry->value, MY_INT64_NUM_DECIMAL_DIGITS);
      item_type= Item::INT_ITEM;
      break;
    case STRING_RESULT:
    {
      const CHARSET_INFO * const fromcs= entry->collation.collation;
      const CHARSET_INFO * const tocs= session->variables.collation_connection;
      uint32_t dummy_offset;

      value.cs_info.character_set_of_placeholder=
        value.cs_info.character_set_client= fromcs;
      /*
        Setup source and destination character sets so that they
        are different only if conversion is necessary: this will
        make later checks easier.
      */
      value.cs_info.final_character_set_of_str_value=
        String::needs_conversion(0, fromcs, tocs, &dummy_offset) ?
        tocs : fromcs;
      /*
        Exact value of max_length is not known unless data is converted to
        charset of connection, so we have to set it later.
      */
      item_type= Item::STRING_ITEM;

      if (set_str((const char *)entry->value, entry->length))
        return(1);
      break;
    }
    case DECIMAL_RESULT:
    {
      const my_decimal *ent_value= (const my_decimal *)entry->value;
      my_decimal2decimal(ent_value, &decimal_value);
      state= DECIMAL_VALUE;
      decimals= ent_value->frac;
      max_length= my_decimal_precision_to_length(ent_value->precision(),
                                                 decimals, unsigned_flag);
      item_type= Item::DECIMAL_ITEM;
      break;
    }
    default:
      assert(0);
      set_null();
    }
  }
  else
    set_null();

  return(0);
}

/**
  Resets parameter after execution.

  @note
    We clear null_value here instead of setting it in set_* methods,
    because we want more easily handle case for long data.
*/

void Item_param::reset()
{
  /* Shrink string buffer if it's bigger than max possible CHAR column */
  if (str_value.alloced_length() > MAX_CHAR_WIDTH)
    str_value.free();
  else
    str_value.length(0);
  str_value_ptr.length(0);
  /*
    We must prevent all charset conversions until data has been written
    to the binary log.
  */
  str_value.set_charset(&my_charset_bin);
  collation.set(&my_charset_bin, DERIVATION_COERCIBLE);
  state= NO_VALUE;
  maybe_null= 1;
  null_value= 0;
  /*
    Don't reset item_type to PARAM_ITEM: it's only needed to guard
    us from item optimizations at prepare stage, when item doesn't yet
    contain a literal of some kind.
    In all other cases when this object is accessed its value is
    set (this assumption is guarded by 'state' and
    assertS(state != NO_VALUE) in all Item_param::get_*
    methods).
  */
  return;
}


int Item_param::save_in_field(Field *field, bool no_conversions)
{
  field->set_notnull();

  switch (state) {
  case INT_VALUE:
    return field->store(value.integer, unsigned_flag);
  case REAL_VALUE:
    return field->store(value.real);
  case DECIMAL_VALUE:
    return field->store_decimal(&decimal_value);
  case TIME_VALUE:
    field->store_time(&value.time, value.time.time_type);
    return 0;
  case STRING_VALUE:
  case LONG_DATA_VALUE:
    return field->store(str_value.ptr(), str_value.length(),
                        str_value.charset());
  case NULL_VALUE:
    return set_field_to_null_with_conversions(field, no_conversions);
  case NO_VALUE:
  default:
    assert(0);
  }
  return 1;
}


bool Item_param::get_time(DRIZZLE_TIME *res)
{
  if (state == TIME_VALUE)
  {
    *res= value.time;
    return 0;
  }
  /*
    If parameter value isn't supplied assertion will fire in val_str()
    which is called from Item::get_time().
  */
  return Item::get_time(res);
}


bool Item_param::get_date(DRIZZLE_TIME *res, uint32_t fuzzydate)
{
  if (state == TIME_VALUE)
  {
    *res= value.time;
    return 0;
  }
  return Item::get_date(res, fuzzydate);
}


double Item_param::val_real()
{
  switch (state) {
  case REAL_VALUE:
    return value.real;
  case INT_VALUE:
    return (double) value.integer;
  case DECIMAL_VALUE:
  {
    double result;
    my_decimal2double(E_DEC_FATAL_ERROR, &decimal_value, &result);
    return result;
  }
  case STRING_VALUE:
  case LONG_DATA_VALUE:
  {
    int dummy_err;
    char *end_not_used;
    return my_strntod(str_value.charset(), (char*) str_value.ptr(),
                      str_value.length(), &end_not_used, &dummy_err);
  }
  case TIME_VALUE:
    /*
      This works for example when user says SELECT ?+0.0 and supplies
      time value for the placeholder.
    */
    return uint64_t2double(TIME_to_uint64_t(&value.time));
  case NULL_VALUE:
    return 0.0;
  default:
    assert(0);
  }
  return 0.0;
}


int64_t Item_param::val_int()
{
  switch (state) {
  case REAL_VALUE:
    return (int64_t) rint(value.real);
  case INT_VALUE:
    return value.integer;
  case DECIMAL_VALUE:
  {
    int64_t i;
    my_decimal2int(E_DEC_FATAL_ERROR, &decimal_value, unsigned_flag, &i);
    return i;
  }
  case STRING_VALUE:
  case LONG_DATA_VALUE:
    {
      int dummy_err;
      return my_strntoll(str_value.charset(), str_value.ptr(),
                         str_value.length(), 10, (char**) 0, &dummy_err);
    }
  case TIME_VALUE:
    return (int64_t) TIME_to_uint64_t(&value.time);
  case NULL_VALUE:
    return 0;
  default:
    assert(0);
  }
  return 0;
}


my_decimal *Item_param::val_decimal(my_decimal *dec)
{
  switch (state) {
  case DECIMAL_VALUE:
    return &decimal_value;
  case REAL_VALUE:
    double2my_decimal(E_DEC_FATAL_ERROR, value.real, dec);
    return dec;
  case INT_VALUE:
    int2my_decimal(E_DEC_FATAL_ERROR, value.integer, unsigned_flag, dec);
    return dec;
  case STRING_VALUE:
  case LONG_DATA_VALUE:
    string2my_decimal(E_DEC_FATAL_ERROR, &str_value, dec);
    return dec;
  case TIME_VALUE:
  {
    int64_t i= (int64_t) TIME_to_uint64_t(&value.time);
    int2my_decimal(E_DEC_FATAL_ERROR, i, 0, dec);
    return dec;
  }
  case NULL_VALUE:
    return 0;
  default:
    assert(0);
  }
  return 0;
}


String *Item_param::val_str(String* str)
{
  switch (state) {
  case STRING_VALUE:
  case LONG_DATA_VALUE:
    return &str_value_ptr;
  case REAL_VALUE:
    str->set_real(value.real, NOT_FIXED_DEC, &my_charset_bin);
    return str;
  case INT_VALUE:
    str->set(value.integer, &my_charset_bin);
    return str;
  case DECIMAL_VALUE:
    if (my_decimal2string(E_DEC_FATAL_ERROR, &decimal_value,
                          0, 0, 0, str) <= 1)
      return str;
    return NULL;
  case TIME_VALUE:
  {
    if (str->reserve(MAX_DATE_STRING_REP_LENGTH))
      break;
    str->length((uint) my_TIME_to_str(&value.time, (char*) str->ptr()));
    str->set_charset(&my_charset_bin);
    return str;
  }
  case NULL_VALUE:
    return NULL;
  default:
    assert(0);
  }
  return str;
}

/**
  Return Param item values in string format, for generating the dynamic
  query used in update/binary logs.

  @todo
    - Change interface and implementation to fill log data in place
    and avoid one more memcpy/alloc between str and log string.
    - In case of error we need to notify replication
    that binary log contains wrong statement
*/

const String *Item_param::query_val_str(String* str) const
{
  switch (state) {
  case INT_VALUE:
    str->set_int(value.integer, unsigned_flag, &my_charset_bin);
    break;
  case REAL_VALUE:
    str->set_real(value.real, NOT_FIXED_DEC, &my_charset_bin);
    break;
  case DECIMAL_VALUE:
    if (my_decimal2string(E_DEC_FATAL_ERROR, &decimal_value,
                          0, 0, 0, str) > 1)
      return &my_null_string;
    break;
  case TIME_VALUE:
    {
      char *buf, *ptr;
      str->length(0);
      /*
        TODO: in case of error we need to notify replication
        that binary log contains wrong statement
      */
      if (str->reserve(MAX_DATE_STRING_REP_LENGTH+3))
        break;

      /* Create date string inplace */
      buf= str->c_ptr_quick();
      ptr= buf;
      *ptr++= '\'';
      ptr+= (uint) my_TIME_to_str(&value.time, ptr);
      *ptr++= '\'';
      str->length((uint32_t) (ptr - buf));
      break;
    }
  case STRING_VALUE:
  case LONG_DATA_VALUE:
    {
      str->length(0);
      append_query_string(value.cs_info.character_set_client, &str_value, str);
      break;
    }
  case NULL_VALUE:
    return &my_null_string;
  default:
    assert(0);
  }
  return str;
}


/**
  Convert string from client character set to the character set of
  connection.
*/

bool Item_param::convert_str_value(Session *session)
{
  bool rc= false;
  if (state == STRING_VALUE || state == LONG_DATA_VALUE)
  {
    /*
      Check is so simple because all charsets were set up properly
      in setup_one_conversion_function, where typecode of
      placeholder was also taken into account: the variables are different
      here only if conversion is really necessary.
    */
    if (value.cs_info.final_character_set_of_str_value !=
        value.cs_info.character_set_of_placeholder)
    {
      rc= session->convert_string(&str_value,
                              value.cs_info.character_set_of_placeholder,
                              value.cs_info.final_character_set_of_str_value);
    }
    else
      str_value.set_charset(value.cs_info.final_character_set_of_str_value);
    /* Here str_value is guaranteed to be in final_character_set_of_str_value */

    max_length= str_value.length();
    decimals= 0;
    /*
      str_value_ptr is returned from val_str(). It must be not alloced
      to prevent it's modification by val_str() invoker.
    */
    str_value_ptr.set(str_value.ptr(), str_value.length(),
                      str_value.charset());
    /* Synchronize item charset with value charset */
    collation.set(str_value.charset(), DERIVATION_COERCIBLE);
  }
  return rc;
}


bool Item_param::basic_const_item() const
{
  if (state == NO_VALUE || state == TIME_VALUE)
    return false;
  return true;
}


Item *
Item_param::clone_item()
{
  /* see comments in the header file */
  switch (state) {
  case NULL_VALUE:
    return new Item_null(name);
  case INT_VALUE:
    return (unsigned_flag ?
            new Item_uint(name, value.integer, max_length) :
            new Item_int(name, value.integer, max_length));
  case REAL_VALUE:
    return new Item_float(name, value.real, decimals, max_length);
  case STRING_VALUE:
  case LONG_DATA_VALUE:
    return new Item_string(name, str_value.c_ptr_quick(), str_value.length(),
                           str_value.charset());
  case TIME_VALUE:
    break;
  case NO_VALUE:
  default:
    assert(0);
  };
  return 0;
}


bool
Item_param::eq(const Item *arg, bool binary_cmp) const
{
  Item *item;
  if (!basic_const_item() || !arg->basic_const_item() || arg->type() != type())
    return false;
  /*
    We need to cast off const to call val_int(). This should be OK for
    a basic constant.
  */
  item= (Item*) arg;

  switch (state) {
  case NULL_VALUE:
    return true;
  case INT_VALUE:
    return value.integer == item->val_int() &&
           unsigned_flag == item->unsigned_flag;
  case REAL_VALUE:
    return value.real == item->val_real();
  case STRING_VALUE:
  case LONG_DATA_VALUE:
    if (binary_cmp)
      return !stringcmp(&str_value, &item->str_value);
    return !sortcmp(&str_value, &item->str_value, collation.collation);
  default:
    break;
  }
  return false;
}

/* End of Item_param related */

void Item_param::print(String *str, enum_query_type)
{
  if (state == NO_VALUE)
  {
    str->append('?');
  }
  else
  {
    char buffer[STRING_BUFFER_USUAL_SIZE];
    String tmp(buffer, sizeof(buffer), &my_charset_bin);
    const String *res;
    res= query_val_str(&tmp);
    str->append(*res);
  }
}


/****************************************************************************
  Item_copy_string
****************************************************************************/

void Item_copy_string::copy()
{
  String *res=item->val_str(&str_value);
  if (res && res != &str_value)
    str_value.copy(*res);
  null_value=item->null_value;
}

/* ARGSUSED */
String *Item_copy_string::val_str(String *)
{
  // Item_copy_string is used without fix_fields call
  if (null_value)
    return (String*) 0;
  return &str_value;
}


my_decimal *Item_copy_string::val_decimal(my_decimal *decimal_value)
{
  // Item_copy_string is used without fix_fields call
  if (null_value)
    return 0;
  string2my_decimal(E_DEC_FATAL_ERROR, &str_value, decimal_value);
  return (decimal_value);
}


/*
  Functions to convert item to field (for send_fields)
*/

/* ARGSUSED */
bool Item::fix_fields(Session *, Item **)
{

  // We do not check fields which are fixed during construction
  assert(fixed == 0 || basic_const_item());
  fixed= 1;
  return false;
}

double Item_ref_null_helper::val_real()
{
  assert(fixed == 1);
  double tmp= (*ref)->val_result();
  owner->was_null|= null_value= (*ref)->null_value;
  return tmp;
}


int64_t Item_ref_null_helper::val_int()
{
  assert(fixed == 1);
  int64_t tmp= (*ref)->val_int_result();
  owner->was_null|= null_value= (*ref)->null_value;
  return tmp;
}


my_decimal *Item_ref_null_helper::val_decimal(my_decimal *decimal_value)
{
  assert(fixed == 1);
  my_decimal *val= (*ref)->val_decimal_result(decimal_value);
  owner->was_null|= null_value= (*ref)->null_value;
  return val;
}


bool Item_ref_null_helper::val_bool()
{
  assert(fixed == 1);
  bool val= (*ref)->val_bool_result();
  owner->was_null|= null_value= (*ref)->null_value;
  return val;
}


String* Item_ref_null_helper::val_str(String* s)
{
  assert(fixed == 1);
  String* tmp= (*ref)->str_result(s);
  owner->was_null|= null_value= (*ref)->null_value;
  return tmp;
}


bool Item_ref_null_helper::get_date(DRIZZLE_TIME *ltime, uint32_t fuzzydate)
{
  return (owner->was_null|= null_value= (*ref)->get_date(ltime, fuzzydate));
}


/**
  Mark item and SELECT_LEXs as dependent if item was resolved in
  outer SELECT.

  @param session             thread handler
  @param last            select from which current item depend
  @param current         current select
  @param resolved_item   item which was resolved in outer SELECT(for warning)
  @param mark_item       item which should be marked (can be differ in case of
                         substitution)
*/

void mark_as_dependent(Session *session, SELECT_LEX *last, SELECT_LEX *current,
                              Item_ident *resolved_item,
                              Item_ident *mark_item)
{
  const char *db_name= (resolved_item->db_name ?
                        resolved_item->db_name : "");
  const char *table_name= (resolved_item->table_name ?
                           resolved_item->table_name : "");
  /* store pointer on SELECT_LEX from which item is dependent */
  if (mark_item)
    mark_item->depended_from= last;
  current->mark_as_dependent(last);
  if (session->lex->describe & DESCRIBE_EXTENDED)
  {
    char warn_buff[DRIZZLE_ERRMSG_SIZE];
    sprintf(warn_buff, ER(ER_WARN_FIELD_RESOLVED),
            db_name, (db_name[0] ? "." : ""),
            table_name, (table_name [0] ? "." : ""),
            resolved_item->field_name,
	    current->select_number, last->select_number);
    push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
		 ER_WARN_FIELD_RESOLVED, warn_buff);
  }
}


/**
  Mark range of selects and resolved identifier (field/reference)
  item as dependent.

  @param session             thread handler
  @param last_select     select where resolved_item was resolved
  @param current_sel     current select (select where resolved_item was placed)
  @param found_field     field which was found during resolving
  @param found_item      Item which was found during resolving (if resolved
                         identifier belongs to VIEW)
  @param resolved_item   Identifier which was resolved

  @note
    We have to mark all items between current_sel (including) and
    last_select (excluding) as dependend (select before last_select should
    be marked with actual table mask used by resolved item, all other with
    OUTER_REF_TABLE_BIT) and also write dependence information to Item of
    resolved identifier.
*/

void mark_select_range_as_dependent(Session *session,
                                    SELECT_LEX *last_select,
                                    SELECT_LEX *current_sel,
                                    Field *found_field, Item *found_item,
                                    Item_ident *resolved_item)
{
  /*
    Go from current SELECT to SELECT where field was resolved (it
    have to be reachable from current SELECT, because it was already
    done once when we resolved this field and cached result of
    resolving)
  */
  SELECT_LEX *previous_select= current_sel;
  for (; previous_select->outer_select() != last_select;
       previous_select= previous_select->outer_select())
  {
    Item_subselect *prev_subselect_item=
      previous_select->master_unit()->item;
    prev_subselect_item->used_tables_cache|= OUTER_REF_TABLE_BIT;
    prev_subselect_item->const_item_cache= 0;
  }
  {
    Item_subselect *prev_subselect_item=
      previous_select->master_unit()->item;
    Item_ident *dependent= resolved_item;
    if (found_field == view_ref_found)
    {
      Item::Type type= found_item->type();
      prev_subselect_item->used_tables_cache|=
        found_item->used_tables();
      dependent= ((type == Item::REF_ITEM || type == Item::FIELD_ITEM) ?
                  (Item_ident*) found_item :
                  0);
    }
    else
      prev_subselect_item->used_tables_cache|=
        found_field->table->map;
    prev_subselect_item->const_item_cache= 0;
    mark_as_dependent(session, last_select, current_sel, resolved_item,
                      dependent);
  }
}


/**
  Search a GROUP BY clause for a field with a certain name.

  Search the GROUP BY list for a column named as find_item. When searching
  preference is given to columns that are qualified with the same table (and
  database) name as the one being searched for.

  @param find_item     the item being searched for
  @param group_list    GROUP BY clause

  @return
    - the found item on success
    - NULL if find_item is not in group_list
*/

static Item** find_field_in_group_list(Item *find_item, order_st *group_list)
{
  const char *db_name;
  const char *table_name;
  const char *field_name;
  order_st      *found_group= NULL;
  int         found_match_degree= 0;
  Item_ident *cur_field;
  int         cur_match_degree= 0;
  char        name_buff[NAME_LEN+1];

  if (find_item->type() == Item::FIELD_ITEM ||
      find_item->type() == Item::REF_ITEM)
  {
    db_name=    ((Item_ident*) find_item)->db_name;
    table_name= ((Item_ident*) find_item)->table_name;
    field_name= ((Item_ident*) find_item)->field_name;
  }
  else
    return NULL;

  if (db_name && lower_case_table_names)
  {
    /* Convert database to lower case for comparison */
    strncpy(name_buff, db_name, sizeof(name_buff)-1);
    my_casedn_str(files_charset_info, name_buff);
    db_name= name_buff;
  }

  assert(field_name != 0);

  for (order_st *cur_group= group_list ; cur_group ; cur_group= cur_group->next)
  {
    if ((*(cur_group->item))->real_item()->type() == Item::FIELD_ITEM)
    {
      cur_field= (Item_ident*) *cur_group->item;
      cur_match_degree= 0;

      assert(cur_field->field_name != 0);

      if (!my_strcasecmp(system_charset_info,
                         cur_field->field_name, field_name))
        ++cur_match_degree;
      else
        continue;

      if (cur_field->table_name && table_name)
      {
        /* If field_name is qualified by a table name. */
        if (my_strcasecmp(table_alias_charset, cur_field->table_name, table_name))
          /* Same field names, different tables. */
          return NULL;

        ++cur_match_degree;
        if (cur_field->db_name && db_name)
        {
          /* If field_name is also qualified by a database name. */
          if (strcmp(cur_field->db_name, db_name))
            /* Same field names, different databases. */
            return NULL;
          ++cur_match_degree;
        }
      }

      if (cur_match_degree > found_match_degree)
      {
        found_match_degree= cur_match_degree;
        found_group= cur_group;
      }
      else if (found_group && (cur_match_degree == found_match_degree) &&
               ! (*(found_group->item))->eq(cur_field, 0))
      {
        /*
          If the current resolve candidate matches equally well as the current
          best match, they must reference the same column, otherwise the field
          is ambiguous.
        */
        my_error(ER_NON_UNIQ_ERROR, MYF(0),
                 find_item->full_name(), current_session->where);
        return NULL;
      }
    }
  }

  if (found_group)
    return found_group->item;
  else
    return NULL;
}


/**
  Resolve a column reference in a sub-select.

  Resolve a column reference (usually inside a HAVING clause) against the
  SELECT and GROUP BY clauses of the query described by 'select'. The name
  resolution algorithm searches both the SELECT and GROUP BY clauses, and in
  case of a name conflict prefers GROUP BY column names over SELECT names. If
  both clauses contain different fields with the same names, a warning is
  issued that name of 'ref' is ambiguous. We extend ANSI SQL in that when no
  GROUP BY column is found, then a HAVING name is resolved as a possibly
  derived SELECT column. This extension is allowed only if the
  MODE_ONLY_FULL_GROUP_BY sql mode isn't enabled.

  @param session     current thread
  @param ref     column reference being resolved
  @param select  the select that ref is resolved against

  @note
    The resolution procedure is:
    - Search for a column or derived column named col_ref_i [in table T_j]
    in the SELECT clause of Q.
    - Search for a column named col_ref_i [in table T_j]
    in the GROUP BY clause of Q.
    - If found different columns with the same name in GROUP BY and SELECT
    - issue a warning and return the GROUP BY column,
    - otherwise
    - if the MODE_ONLY_FULL_GROUP_BY mode is enabled return error
    - else return the found SELECT column.


  @return
    - NULL - there was an error, and the error was already reported
    - not_found_item - the item was not resolved, no error was reported
    - resolved item - if the item was resolved
*/

Item**
resolve_ref_in_select_and_group(Session *session, Item_ident *ref, SELECT_LEX *select)
{
  Item **group_by_ref= NULL;
  Item **select_ref= NULL;
  order_st *group_list= (order_st*) select->group_list.first;
  bool ambiguous_fields= false;
  uint32_t counter;
  enum_resolution_type resolution;

  /*
    Search for a column or derived column named as 'ref' in the SELECT
    clause of the current select.
  */
  if (!(select_ref= find_item_in_list(ref, *(select->get_item_list()),
                                      &counter, REPORT_EXCEPT_NOT_FOUND,
                                      &resolution)))
    return NULL; /* Some error occurred. */
  if (resolution == RESOLVED_AGAINST_ALIAS)
    ref->alias_name_used= true;

  /* If this is a non-aggregated field inside HAVING, search in GROUP BY. */
  if (select->having_fix_field && !ref->with_sum_func && group_list)
  {
    group_by_ref= find_field_in_group_list(ref, group_list);

    /* Check if the fields found in SELECT and GROUP BY are the same field. */
    if (group_by_ref && (select_ref != not_found_item) &&
        !((*group_by_ref)->eq(*select_ref, 0)))
    {
      ambiguous_fields= true;
      push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_WARN, ER_NON_UNIQ_ERROR,
                          ER(ER_NON_UNIQ_ERROR), ref->full_name(),
                          current_session->where);

    }
  }

  if (select_ref != not_found_item || group_by_ref)
  {
    if (select_ref != not_found_item && !ambiguous_fields)
    {
      assert(*select_ref != 0);
      if (!select->ref_pointer_array[counter])
      {
        my_error(ER_ILLEGAL_REFERENCE, MYF(0),
                 ref->name, "forward reference in item list");
        return NULL;
      }
      assert((*select_ref)->fixed);
      return (select->ref_pointer_array + counter);
    }
    if (group_by_ref)
      return group_by_ref;
    assert(false);
    return NULL; /* So there is no compiler warning. */
  }

  return (Item**) not_found_item;
}

void Item::init_make_field(Send_field *tmp_field,
			   enum enum_field_types field_type_arg)
{
  char *empty_name= (char*) "";
  tmp_field->db_name=		empty_name;
  tmp_field->org_table_name=	empty_name;
  tmp_field->org_col_name=	empty_name;
  tmp_field->table_name=	empty_name;
  tmp_field->col_name=		name;
  tmp_field->charsetnr=         collation.collation->number;
  tmp_field->flags=             (maybe_null ? 0 : NOT_NULL_FLAG) |
                                (my_binary_compare(collation.collation) ?
                                 BINARY_FLAG : 0);
  tmp_field->type=              field_type_arg;
  tmp_field->length=max_length;
  tmp_field->decimals=decimals;
}

void Item::make_field(Send_field *tmp_field)
{
  init_make_field(tmp_field, field_type());
}


enum_field_types Item::string_field_type() const
{
  enum_field_types f_type= DRIZZLE_TYPE_VARCHAR;
  if (max_length >= 65536)
    f_type= DRIZZLE_TYPE_BLOB;
  return f_type;
}


void Item_empty_string::make_field(Send_field *tmp_field)
{
  init_make_field(tmp_field, string_field_type());
}


enum_field_types Item::field_type() const
{
  switch (result_type()) {
  case STRING_RESULT:  return string_field_type();
  case INT_RESULT:     return DRIZZLE_TYPE_LONGLONG;
  case DECIMAL_RESULT: return DRIZZLE_TYPE_NEWDECIMAL;
  case REAL_RESULT:    return DRIZZLE_TYPE_DOUBLE;
  case ROW_RESULT:
  default:
    assert(0);
    return DRIZZLE_TYPE_VARCHAR;
  }
}


bool Item::is_datetime()
{
  switch (field_type())
  {
    case DRIZZLE_TYPE_DATE:
    case DRIZZLE_TYPE_DATETIME:
    case DRIZZLE_TYPE_TIMESTAMP:
      return true;
    default:
      break;
  }
  return false;
}


String *Item::check_well_formed_result(String *str, bool send_error)
{
  /* Check whether we got a well-formed string */
  const CHARSET_INFO * const cs= str->charset();
  int well_formed_error;
  uint32_t wlen= cs->cset->well_formed_len(cs,
                                       str->ptr(), str->ptr() + str->length(),
                                       str->length(), &well_formed_error);
  if (wlen < str->length())
  {
    Session *session= current_session;
    char hexbuf[7];
    enum DRIZZLE_ERROR::enum_warning_level level;
    uint32_t diff= str->length() - wlen;
    set_if_smaller(diff, 3);
    octet2hex(hexbuf, str->ptr() + wlen, diff);
    if (send_error)
    {
      my_error(ER_INVALID_CHARACTER_STRING, MYF(0),
               cs->csname,  hexbuf);
      return 0;
    }
    {
      level= DRIZZLE_ERROR::WARN_LEVEL_ERROR;
      null_value= 1;
      str= 0;
    }
    push_warning_printf(session, level, ER_INVALID_CHARACTER_STRING,
                        ER(ER_INVALID_CHARACTER_STRING), cs->csname, hexbuf);
  }
  return str;
}

/*
  Compare two items using a given collation

  SYNOPSIS
    eq_by_collation()
    item               item to compare with
    binary_cmp         true <-> compare as binaries
    cs                 collation to use when comparing strings

  DESCRIPTION
    This method works exactly as Item::eq if the collation cs coincides with
    the collation of the compared objects. Otherwise, first the collations that
    differ from cs are replaced for cs and then the items are compared by
    Item::eq. After the comparison the original collations of items are
    restored.

  RETURN
    1    compared items has been detected as equal
    0    otherwise
*/

bool Item::eq_by_collation(Item *item, bool binary_cmp, const CHARSET_INFO * const cs)
{
  const CHARSET_INFO *save_cs= 0;
  const CHARSET_INFO *save_item_cs= 0;
  if (collation.collation != cs)
  {
    save_cs= collation.collation;
    collation.collation= cs;
  }
  if (item->collation.collation != cs)
  {
    save_item_cs= item->collation.collation;
    item->collation.collation= cs;
  }
  bool res= eq(item, binary_cmp);
  if (save_cs)
    collation.collation= save_cs;
  if (save_item_cs)
    item->collation.collation= save_item_cs;
  return res;
}


/**
  Create a field to hold a string value from an item.

  If max_length > CONVERT_IF_BIGGER_TO_BLOB create a blob @n
  If max_length > 0 create a varchar @n
  If max_length == 0 create a CHAR(0)

  @param table		Table for which the field is created
*/

Field *Item::make_string_field(Table *table)
{
  Field *field;
  assert(collation.collation);
  if (max_length/collation.collation->mbmaxlen > CONVERT_IF_BIGGER_TO_BLOB)
    field= new Field_blob(max_length, maybe_null, name,
                          collation.collation);
  else
    field= new Field_varstring(max_length, maybe_null, name, table->s,
                               collation.collation);

  if (field)
    field->init(table);
  return field;
}


/**
  Create a field based on field_type of argument.

  For now, this is only used to create a field for
  IFNULL(x,something) and time functions

  @retval
    NULL  error
  @retval
    \#    Created field
*/

Field *Item::tmp_table_field_from_field_type(Table *table, bool)
{
  /*
    The field functions defines a field to be not null if null_ptr is not 0
  */
  unsigned char *null_ptr= maybe_null ? (unsigned char*) "" : 0;
  Field *field;

  switch (field_type()) {
  case DRIZZLE_TYPE_NEWDECIMAL:
    field= new Field_new_decimal((unsigned char*) 0, max_length, null_ptr, 0,
                                 Field::NONE, name, decimals, 0,
                                 unsigned_flag);
    break;
  case DRIZZLE_TYPE_LONG:
    field= new Field_long((unsigned char*) 0, max_length, null_ptr, 0, Field::NONE,
			  name, 0, unsigned_flag);
    break;
  case DRIZZLE_TYPE_LONGLONG:
    field= new Field_int64_t((unsigned char*) 0, max_length, null_ptr, 0, Field::NONE,
			      name, 0, unsigned_flag);
    break;
  case DRIZZLE_TYPE_DOUBLE:
    field= new Field_double((unsigned char*) 0, max_length, null_ptr, 0, Field::NONE,
			    name, decimals, 0, unsigned_flag);
    break;
  case DRIZZLE_TYPE_NULL:
    field= new Field_null((unsigned char*) 0, max_length, Field::NONE,
			  name, &my_charset_bin);
    break;
  case DRIZZLE_TYPE_DATE:
    field= new Field_date(maybe_null, name, &my_charset_bin);
    break;
  case DRIZZLE_TYPE_TIME:
    field= new Field_time(maybe_null, name, &my_charset_bin);
    break;
  case DRIZZLE_TYPE_TIMESTAMP:
    field= new Field_timestamp(maybe_null, name, &my_charset_bin);
    break;
  case DRIZZLE_TYPE_DATETIME:
    field= new Field_datetime(maybe_null, name, &my_charset_bin);
    break;
  default:
    /* This case should never be chosen */
    assert(0);
    /* Fall through to make_string_field() */
  case DRIZZLE_TYPE_ENUM:
  case DRIZZLE_TYPE_VARCHAR:
    return make_string_field(table);
  case DRIZZLE_TYPE_BLOB:
    if (this->type() == Item::TYPE_HOLDER)
      field= new Field_blob(max_length, maybe_null, name, collation.collation,
                            1);
    else
      field= new Field_blob(max_length, maybe_null, name, collation.collation);
    break;					// Blob handled outside of case
  }
  if (field)
    field->init(table);
  return field;
}


/**
  Store null in field.

  This is used on INSERT.
  Allow NULL to be inserted in timestamp and auto_increment values.

  @param field		Field where we want to store NULL

  @retval
    0   ok
  @retval
    1   Field doesn't support NULL values and can't handle 'field = NULL'
*/

int Item_null::save_in_field(Field *field, bool no_conversions)
{
  return set_field_to_null_with_conversions(field, no_conversions);
}


/**
  Store null in field.

  @param field		Field where we want to store NULL

  @retval
    0	 OK
  @retval
    1	 Field doesn't support NULL values
*/

int Item_null::save_safe_in_field(Field *field)
{
  return set_field_to_null(field);
}


/*
  This implementation can lose str_value content, so if the
  Item uses str_value to store something, it should
  reimplement it's ::save_in_field() as Item_string, for example, does
*/

int Item::save_in_field(Field *field, bool no_conversions)
{
  int error;
  if (result_type() == STRING_RESULT)
  {
    String *result;
    const CHARSET_INFO * const cs= collation.collation;
    char buff[MAX_FIELD_WIDTH];		// Alloc buffer for small columns
    str_value.set_quick(buff, sizeof(buff), cs);
    result=val_str(&str_value);
    if (null_value)
    {
      str_value.set_quick(0, 0, cs);
      return set_field_to_null_with_conversions(field, no_conversions);
    }

    /* NOTE: If null_value == false, "result" must be not NULL.  */

    field->set_notnull();
    error=field->store(result->ptr(),result->length(),cs);
    str_value.set_quick(0, 0, cs);
  }
  else if (result_type() == REAL_RESULT &&
           field->result_type() == STRING_RESULT)
  {
    double nr= val_real();
    if (null_value)
      return set_field_to_null_with_conversions(field, no_conversions);
    field->set_notnull();
    error= field->store(nr);
  }
  else if (result_type() == REAL_RESULT)
  {
    double nr= val_real();
    if (null_value)
      return set_field_to_null(field);
    field->set_notnull();
    error=field->store(nr);
  }
  else if (result_type() == DECIMAL_RESULT)
  {
    my_decimal decimal_value;
    my_decimal *value= val_decimal(&decimal_value);
    if (null_value)
      return set_field_to_null_with_conversions(field, no_conversions);
    field->set_notnull();
    error=field->store_decimal(value);
  }
  else
  {
    int64_t nr=val_int();
    if (null_value)
      return set_field_to_null_with_conversions(field, no_conversions);
    field->set_notnull();
    error=field->store(nr, unsigned_flag);
  }
  return error;
}


int Item_string::save_in_field(Field *field, bool)
{
  String *result;
  result=val_str(&str_value);
  return save_str_value_in_field(field, result);
}


int Item_uint::save_in_field(Field *field, bool no_conversions)
{
  /* Item_int::save_in_field handles both signed and unsigned. */
  return Item_int::save_in_field(field, no_conversions);
}


int Item_int::save_in_field(Field *field, bool)
{
  int64_t nr=val_int();
  if (null_value)
    return set_field_to_null(field);
  field->set_notnull();
  return field->store(nr, unsigned_flag);
}


int Item_decimal::save_in_field(Field *field, bool)
{
  field->set_notnull();
  return field->store_decimal(&decimal_value);
}


bool Item_int::eq(const Item *arg, bool) const
{
  /* No need to check for null value as basic constant can't be NULL */
  if (arg->basic_const_item() && arg->type() == type())
  {
    /*
      We need to cast off const to call val_int(). This should be OK for
      a basic constant.
    */
    Item *item= (Item*) arg;
    return item->val_int() == value && item->unsigned_flag == unsigned_flag;
  }
  return false;
}


Item *Item_int_with_ref::clone_item()
{
  assert(ref->const_item());
  /*
    We need to evaluate the constant to make sure it works with
    parameter markers.
  */
  return (ref->unsigned_flag ?
          new Item_uint(ref->name, ref->val_int(), ref->max_length) :
          new Item_int(ref->name, ref->val_int(), ref->max_length));
}


static uint32_t nr_of_decimals(const char *str, const char *end)
{
  const char *decimal_point;

  /* Find position for '.' */
  for (;;)
  {
    if (str == end)
      return 0;
    if (*str == 'e' || *str == 'E')
      return NOT_FIXED_DEC;
    if (*str++ == '.')
      break;
  }
  decimal_point= str;
  for (; my_isdigit(system_charset_info, *str) ; str++)
    ;
  if (*str == 'e' || *str == 'E')
    return NOT_FIXED_DEC;
  return (uint) (str - decimal_point);
}


/**
  This function is only called during parsing. We will signal an error if
  value is not a true double value (overflow)
*/

Item_float::Item_float(const char *str_arg, uint32_t length)
{
  int error;
  char *end_not_used;
  value= my_strntod(&my_charset_bin, (char*) str_arg, length, &end_not_used,
                    &error);
  if (error)
  {
    /*
      Note that we depend on that str_arg is null terminated, which is true
      when we are in the parser
    */
    assert(str_arg[length] == 0);
    my_error(ER_ILLEGAL_VALUE_FOR_TYPE, MYF(0), "double", (char*) str_arg);
  }
  presentation= name=(char*) str_arg;
  decimals=(uint8_t) nr_of_decimals(str_arg, str_arg+length);
  max_length=length;
  fixed= 1;
}


int Item_float::save_in_field(Field *field, bool)
{
  double nr= val_real();
  if (null_value)
    return set_field_to_null(field);
  field->set_notnull();
  return field->store(nr);
}


void Item_float::print(String *str, enum_query_type)
{
  if (presentation)
  {
    str->append(presentation);
    return;
  }
  char buffer[20];
  String num(buffer, sizeof(buffer), &my_charset_bin);
  num.set_real(value, decimals, &my_charset_bin);
  str->append(num);
}


/*
  hex item
  In string context this is a binary string.
  In number context this is a int64_t value.
*/

bool Item_float::eq(const Item *arg, bool) const
{
  if (arg->basic_const_item() && arg->type() == type())
  {
    /*
      We need to cast off const to call val_int(). This should be OK for
      a basic constant.
    */
    Item *item= (Item*) arg;
    return item->val_real() == value;
  }
  return false;
}


inline uint32_t char_val(char X)
{
  return (uint) (X >= '0' && X <= '9' ? X-'0' :
		 X >= 'A' && X <= 'Z' ? X-'A'+10 :
		 X-'a'+10);
}


Item_hex_string::Item_hex_string(const char *str, uint32_t str_length)
{
  max_length=(str_length+1)/2;
  char *ptr=(char*) sql_alloc(max_length+1);
  if (!ptr)
    return;
  str_value.set(ptr,max_length,&my_charset_bin);
  char *end=ptr+max_length;
  if (max_length*2 != str_length)
    *ptr++=char_val(*str++);			// Not even, assume 0 prefix
  while (ptr != end)
  {
    *ptr++= (char) (char_val(str[0])*16+char_val(str[1]));
    str+=2;
  }
  *ptr=0;					// Keep purify happy
  collation.set(&my_charset_bin, DERIVATION_COERCIBLE);
  fixed= 1;
  unsigned_flag= 1;
}

int64_t Item_hex_string::val_int()
{
  // following assert is redundant, because fixed=1 assigned in constructor
  assert(fixed == 1);
  char *end=(char*) str_value.ptr()+str_value.length(),
       *ptr=end-cmin(str_value.length(),(uint32_t)sizeof(int64_t));

  uint64_t value=0;
  for (; ptr != end ; ptr++)
    value=(value << 8)+ (uint64_t) (unsigned char) *ptr;
  return (int64_t) value;
}


my_decimal *Item_hex_string::val_decimal(my_decimal *decimal_value)
{
  // following assert is redundant, because fixed=1 assigned in constructor
  assert(fixed == 1);
  uint64_t value= (uint64_t)val_int();
  int2my_decimal(E_DEC_FATAL_ERROR, value, true, decimal_value);
  return (decimal_value);
}


int Item_hex_string::save_in_field(Field *field, bool)
{
  field->set_notnull();
  if (field->result_type() == STRING_RESULT)
    return field->store(str_value.ptr(), str_value.length(),
                        collation.collation);

  uint64_t nr;
  uint32_t length= str_value.length();
  if (length > 8)
  {
    nr= field->flags & UNSIGNED_FLAG ? UINT64_MAX : INT64_MAX;
    goto warn;
  }
  nr= (uint64_t) val_int();
  if ((length == 8) && !(field->flags & UNSIGNED_FLAG) && (nr > INT64_MAX))
  {
    nr= INT64_MAX;
    goto warn;
  }
  return field->store((int64_t) nr, true);  // Assume hex numbers are unsigned

warn:
  if (!field->store((int64_t) nr, true))
    field->set_warning(DRIZZLE_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE,
                       1);
  return 1;
}


void Item_hex_string::print(String *str, enum_query_type)
{
  char *end= (char*) str_value.ptr() + str_value.length(),
       *ptr= end - cmin(str_value.length(), (uint32_t)sizeof(int64_t));
  str->append("0x");
  for (; ptr != end ; ptr++)
  {
    str->append(_dig_vec_lower[((unsigned char) *ptr) >> 4]);
    str->append(_dig_vec_lower[((unsigned char) *ptr) & 0x0F]);
  }
}


bool Item_hex_string::eq(const Item *arg, bool binary_cmp) const
{
  if (arg->basic_const_item() && arg->type() == type())
  {
    if (binary_cmp)
      return !stringcmp(&str_value, &arg->str_value);
    return !sortcmp(&str_value, &arg->str_value, collation.collation);
  }
  return false;
}


Item *Item_hex_string::safe_charset_converter(const CHARSET_INFO * const tocs)
{
  Item_string *conv;
  String tmp, *str= val_str(&tmp);

  if (!(conv= new Item_string(str->ptr(), str->length(), tocs)))
    return NULL;
  conv->str_value.copy();
  conv->str_value.mark_as_const();
  return conv;
}


/*
  bin item.
  In string context this is a binary string.
  In number context this is a int64_t value.
*/

Item_bin_string::Item_bin_string(const char *str, uint32_t str_length)
{
  const char *end= str + str_length - 1;
  unsigned char bits= 0;
  uint32_t power= 1;

  max_length= (str_length + 7) >> 3;
  char *ptr= (char*) sql_alloc(max_length + 1);
  if (!ptr)
    return;
  str_value.set(ptr, max_length, &my_charset_bin);
  ptr+= max_length - 1;
  ptr[1]= 0;                     // Set end null for string
  for (; end >= str; end--)
  {
    if (power == 256)
    {
      power= 1;
      *ptr--= bits;
      bits= 0;
    }
    if (*end == '1')
      bits|= power;
    power<<= 1;
  }
  *ptr= (char) bits;
  collation.set(&my_charset_bin, DERIVATION_COERCIBLE);
  fixed= 1;
}


/**
  Pack data in buffer for sending.
*/

bool Item_null::send(Protocol *protocol,
                     String *)
{
  return protocol->store_null();
}

/**
  This is only called from items that is not of type item_field.
*/

bool Item::send(Protocol *protocol, String *buffer)
{
  bool result= false;
  enum_field_types f_type;

  switch ((f_type=field_type())) {
  default:
  case DRIZZLE_TYPE_NULL:
  case DRIZZLE_TYPE_ENUM:
  case DRIZZLE_TYPE_BLOB:
  case DRIZZLE_TYPE_VARCHAR:
  case DRIZZLE_TYPE_NEWDECIMAL:
  {
    String *res;
    if ((res=val_str(buffer)))
      result= protocol->store(res->ptr(),res->length(),res->charset());
    break;
  }
  case DRIZZLE_TYPE_LONG:
  {
    int64_t nr;
    nr= val_int();
    if (!null_value)
      result= protocol->store_long(nr);
    break;
  }
  case DRIZZLE_TYPE_LONGLONG:
  {
    int64_t nr;
    nr= val_int();
    if (!null_value)
      result= protocol->store_int64_t(nr, unsigned_flag);
    break;
  }
  case DRIZZLE_TYPE_DOUBLE:
  {
    double nr= val_real();
    if (!null_value)
      result= protocol->store(nr, decimals, buffer);
    break;
  }
  case DRIZZLE_TYPE_DATETIME:
  case DRIZZLE_TYPE_TIMESTAMP:
  {
    DRIZZLE_TIME tm;
    get_date(&tm, TIME_FUZZY_DATE);
    if (!null_value)
    {
      if (f_type == DRIZZLE_TYPE_DATE)
	return protocol->store_date(&tm);
      else
	result= protocol->store(&tm);
    }
    break;
  }
  case DRIZZLE_TYPE_TIME:
  {
    DRIZZLE_TIME tm;
    get_time(&tm);
    if (!null_value)
      result= protocol->store_time(&tm);
    break;
  }
  }
  if (null_value)
    result= protocol->store_null();
  return result;
}


Item_ref::Item_ref(Name_resolution_context *context_arg,
                   Item **item, const char *table_name_arg,
                   const char *field_name_arg,
                   bool alias_name_used_arg)
  :Item_ident(context_arg, NULL, table_name_arg, field_name_arg),
   result_field(0), ref(item)
{
  alias_name_used= alias_name_used_arg;
  /*
    This constructor used to create some internals references over fixed items
  */
  if (ref && *ref && (*ref)->fixed)
    set_properties();
}


/**
  Resolve the name of a reference to a column reference.

  The method resolves the column reference represented by 'this' as a column
  present in one of: GROUP BY clause, SELECT clause, outer queries. It is
  used typically for columns in the HAVING clause which are not under
  aggregate functions.

  POSTCONDITION @n
  Item_ref::ref is 0 or points to a valid item.

  @note
    The name resolution algorithm used is (where [T_j] is an optional table
    name that qualifies the column name):

  @code
        resolve_extended([T_j].col_ref_i)
        {
          Search for a column or derived column named col_ref_i [in table T_j]
          in the SELECT and GROUP clauses of Q.

          if such a column is NOT found AND    // Lookup in outer queries.
             there are outer queries
          {
            for each outer query Q_k beginning from the inner-most one
           {
              Search for a column or derived column named col_ref_i
              [in table T_j] in the SELECT and GROUP clauses of Q_k.

              if such a column is not found AND
                 - Q_k is not a group query AND
                 - Q_k is not inside an aggregate function
                 OR
                 - Q_(k-1) is not in a HAVING or SELECT clause of Q_k
              {
                search for a column or derived column named col_ref_i
                [in table T_j] in the FROM clause of Q_k;
              }
            }
          }
        }
  @endcode
  @n
    This procedure treats GROUP BY and SELECT clauses as one namespace for
    column references in HAVING. Notice that compared to
    Item_field::fix_fields, here we first search the SELECT and GROUP BY
    clauses, and then we search the FROM clause.

  @param[in]     session        current thread
  @param[in,out] reference  view column if this item was resolved to a
    view column

  @todo
    Here we could first find the field anyway, and then test this
    condition, so that we can give a better error message -
    ER_WRONG_FIELD_WITH_GROUP, instead of the less informative
    ER_BAD_FIELD_ERROR which we produce now.

  @retval
    true  if error
  @retval
    false on success
*/

bool Item_ref::fix_fields(Session *session, Item **reference)
{
  enum_parsing_place place= NO_MATTER;
  assert(fixed == 0);
  SELECT_LEX *current_sel= session->lex->current_select;

  if (!ref || ref == not_found_item)
  {
    if (!(ref= resolve_ref_in_select_and_group(session, this,
                                               context->select_lex)))
      goto error;             /* Some error occurred (e.g. ambiguous names). */

    if (ref == not_found_item) /* This reference was not resolved. */
    {
      Name_resolution_context *last_checked_context= context;
      Name_resolution_context *outer_context= context->outer_context;
      Field *from_field;
      ref= 0;

      if (!outer_context)
      {
        /* The current reference cannot be resolved in this query. */
        my_error(ER_BAD_FIELD_ERROR,MYF(0),
                 this->full_name(), current_session->where);
        goto error;
      }

      /*
        If there is an outer context (select), and it is not a derived table
        (which do not support the use of outer fields for now), try to
        resolve this reference in the outer select(s).

        We treat each subselect as a separate namespace, so that different
        subselects may contain columns with the same names. The subselects are
        searched starting from the innermost.
      */
      from_field= (Field*) not_found_field;

      do
      {
        SELECT_LEX *select= outer_context->select_lex;
        Item_subselect *prev_subselect_item=
          last_checked_context->select_lex->master_unit()->item;
        last_checked_context= outer_context;

        /* Search in the SELECT and GROUP lists of the outer select. */
        if (outer_context->resolve_in_select_list)
        {
          if (!(ref= resolve_ref_in_select_and_group(session, this, select)))
            goto error; /* Some error occurred (e.g. ambiguous names). */
          if (ref != not_found_item)
          {
            assert(*ref && (*ref)->fixed);
            prev_subselect_item->used_tables_cache|= (*ref)->used_tables();
            prev_subselect_item->const_item_cache&= (*ref)->const_item();
            break;
          }
          /*
            Set ref to 0 to ensure that we get an error in case we replaced
            this item with another item and still use this item in some
            other place of the parse tree.
          */
          ref= 0;
        }

        place= prev_subselect_item->parsing_place;
        /*
          Check table fields only if the subquery is used somewhere out of
          HAVING or the outer SELECT does not use grouping (i.e. tables are
          accessible).
          TODO:
          Here we could first find the field anyway, and then test this
          condition, so that we can give a better error message -
          ER_WRONG_FIELD_WITH_GROUP, instead of the less informative
          ER_BAD_FIELD_ERROR which we produce now.
        */
        if ((place != IN_HAVING ||
             (!select->with_sum_func &&
              select->group_list.elements == 0)))
        {
          /*
            In case of view, find_field_in_tables() write pointer to view
            field expression to 'reference', i.e. it substitute that
            expression instead of this Item_ref
          */
          from_field= find_field_in_tables(session, this,
                                           outer_context->
                                             first_name_resolution_table,
                                           outer_context->
                                             last_name_resolution_table,
                                           reference,
                                           IGNORE_EXCEPT_NON_UNIQUE,
                                           true, true);
          if (! from_field)
            goto error;
          if (from_field == view_ref_found)
          {
            Item::Type refer_type= (*reference)->type();
            prev_subselect_item->used_tables_cache|=
              (*reference)->used_tables();
            prev_subselect_item->const_item_cache&=
              (*reference)->const_item();
            assert((*reference)->type() == REF_ITEM);
            mark_as_dependent(session, last_checked_context->select_lex,
                              context->select_lex, this,
                              ((refer_type == REF_ITEM ||
                                refer_type == FIELD_ITEM) ?
                               (Item_ident*) (*reference) :
                               0));
            /*
              view reference found, we substituted it instead of this
              Item, so can quit
            */
            return false;
          }
          if (from_field != not_found_field)
          {
            if (cached_table && cached_table->select_lex &&
                outer_context->select_lex &&
                cached_table->select_lex != outer_context->select_lex)
            {
              /*
                Due to cache, find_field_in_tables() can return field which
                doesn't belong to provided outer_context. In this case we have
                to find proper field context in order to fix field correcly.
              */
              do
              {
                outer_context= outer_context->outer_context;
                select= outer_context->select_lex;
                prev_subselect_item=
                  last_checked_context->select_lex->master_unit()->item;
                last_checked_context= outer_context;
              } while (outer_context && outer_context->select_lex &&
                       cached_table->select_lex != outer_context->select_lex);
            }
            prev_subselect_item->used_tables_cache|= from_field->table->map;
            prev_subselect_item->const_item_cache= 0;
            break;
          }
        }
        assert(from_field == not_found_field);

        /* Reference is not found => depend on outer (or just error). */
        prev_subselect_item->used_tables_cache|= OUTER_REF_TABLE_BIT;
        prev_subselect_item->const_item_cache= 0;

        outer_context= outer_context->outer_context;
      } while (outer_context);

      assert(from_field != 0 && from_field != view_ref_found);
      if (from_field != not_found_field)
      {
        Item_field* fld;
        if (!(fld= new Item_field(from_field)))
          goto error;
        session->change_item_tree(reference, fld);
        mark_as_dependent(session, last_checked_context->select_lex,
                          session->lex->current_select, this, fld);
        /*
          A reference is resolved to a nest level that's outer or the same as
          the nest level of the enclosing set function : adjust the value of
          max_arg_level for the function if it's needed.
        */
        if (session->lex->in_sum_func &&
            session->lex->in_sum_func->nest_level >=
            last_checked_context->select_lex->nest_level)
          set_if_bigger(session->lex->in_sum_func->max_arg_level,
                        last_checked_context->select_lex->nest_level);
        return false;
      }
      if (ref == 0)
      {
        /* The item was not a table field and not a reference */
        my_error(ER_BAD_FIELD_ERROR, MYF(0),
                 this->full_name(), current_session->where);
        goto error;
      }
      /* Should be checked in resolve_ref_in_select_and_group(). */
      assert(*ref && (*ref)->fixed);
      mark_as_dependent(session, last_checked_context->select_lex,
                        context->select_lex, this, this);
      /*
        A reference is resolved to a nest level that's outer or the same as
        the nest level of the enclosing set function : adjust the value of
        max_arg_level for the function if it's needed.
      */
      if (session->lex->in_sum_func &&
          session->lex->in_sum_func->nest_level >=
          last_checked_context->select_lex->nest_level)
        set_if_bigger(session->lex->in_sum_func->max_arg_level,
                      last_checked_context->select_lex->nest_level);
    }
  }

  assert(*ref);
  /*
    Check if this is an incorrect reference in a group function or forward
    reference. Do not issue an error if this is:
      1. outer reference (will be fixed later by the fix_inner_refs function);
      2. an unnamed reference inside an aggregate function.
  */
  if (!((*ref)->type() == REF_ITEM &&
       ((Item_ref *)(*ref))->ref_type() == OUTER_REF) &&
      (((*ref)->with_sum_func && name &&
        !(current_sel->linkage != GLOBAL_OPTIONS_TYPE &&
          current_sel->having_fix_field)) ||
       !(*ref)->fixed))
  {
    my_error(ER_ILLEGAL_REFERENCE, MYF(0),
             name, ((*ref)->with_sum_func?
                    "reference to group function":
                    "forward reference in item list"));
    goto error;
  }

  set_properties();

  if ((*ref)->check_cols(1))
    goto error;
  return false;

error:
  context->process_error(session);
  return true;
}


void Item_ref::set_properties()
{
  max_length= (*ref)->max_length;
  maybe_null= (*ref)->maybe_null;
  decimals=   (*ref)->decimals;
  collation.set((*ref)->collation);
  /*
    We have to remember if we refer to a sum function, to ensure that
    split_sum_func() doesn't try to change the reference.
  */
  with_sum_func= (*ref)->with_sum_func;
  unsigned_flag= (*ref)->unsigned_flag;
  fixed= 1;
  if (alias_name_used)
    return;
  if ((*ref)->type() == FIELD_ITEM)
    alias_name_used= ((Item_ident *) (*ref))->alias_name_used;
  else
    alias_name_used= true; // it is not field, so it is was resolved by alias
}


void Item_ref::cleanup()
{
  Item_ident::cleanup();
  result_field= 0;
  return;
}


void Item_ref::print(String *str, enum_query_type query_type)
{
  if (ref)
  {
    if ((*ref)->type() != Item::CACHE_ITEM &&
        !table_name && name && alias_name_used)
    {
      Session *session= current_session;
      append_identifier(session, str, name, (uint) strlen(name));
    }
    else
      (*ref)->print(str, query_type);
  }
  else
    Item_ident::print(str, query_type);
}


bool Item_ref::send(Protocol *prot, String *tmp)
{
  if (result_field)
    return prot->store(result_field);
  return (*ref)->send(prot, tmp);
}


double Item_ref::val_result()
{
  if (result_field)
  {
    if ((null_value= result_field->is_null()))
      return 0.0;
    return result_field->val_real();
  }
  return val_real();
}


int64_t Item_ref::val_int_result()
{
  if (result_field)
  {
    if ((null_value= result_field->is_null()))
      return 0;
    return result_field->val_int();
  }
  return val_int();
}


String *Item_ref::str_result(String* str)
{
  if (result_field)
  {
    if ((null_value= result_field->is_null()))
      return 0;
    str->set_charset(str_value.charset());
    return result_field->val_str(str, &str_value);
  }
  return val_str(str);
}


my_decimal *Item_ref::val_decimal_result(my_decimal *decimal_value)
{
  if (result_field)
  {
    if ((null_value= result_field->is_null()))
      return 0;
    return result_field->val_decimal(decimal_value);
  }
  return val_decimal(decimal_value);
}


bool Item_ref::val_bool_result()
{
  if (result_field)
  {
    if ((null_value= result_field->is_null()))
      return 0;
    switch (result_field->result_type()) {
    case INT_RESULT:
      return result_field->val_int() != 0;
    case DECIMAL_RESULT:
    {
      my_decimal decimal_value;
      my_decimal *val= result_field->val_decimal(&decimal_value);
      if (val)
        return !my_decimal_is_zero(val);
      return 0;
    }
    case REAL_RESULT:
    case STRING_RESULT:
      return result_field->val_real() != 0.0;
    case ROW_RESULT:
    default:
      assert(0);
    }
  }
  return val_bool();
}


double Item_ref::val_real()
{
  assert(fixed);
  double tmp=(*ref)->val_result();
  null_value=(*ref)->null_value;
  return tmp;
}


int64_t Item_ref::val_int()
{
  assert(fixed);
  int64_t tmp=(*ref)->val_int_result();
  null_value=(*ref)->null_value;
  return tmp;
}


bool Item_ref::val_bool()
{
  assert(fixed);
  bool tmp= (*ref)->val_bool_result();
  null_value= (*ref)->null_value;
  return tmp;
}


String *Item_ref::val_str(String* tmp)
{
  assert(fixed);
  tmp=(*ref)->str_result(tmp);
  null_value=(*ref)->null_value;
  return tmp;
}


bool Item_ref::is_null()
{
  assert(fixed);
  return (*ref)->is_null();
}


bool Item_ref::get_date(DRIZZLE_TIME *ltime,uint32_t fuzzydate)
{
  return (null_value=(*ref)->get_date_result(ltime,fuzzydate));
}


my_decimal *Item_ref::val_decimal(my_decimal *decimal_value)
{
  my_decimal *val= (*ref)->val_decimal_result(decimal_value);
  null_value= (*ref)->null_value;
  return val;
}

int Item_ref::save_in_field(Field *to, bool no_conversions)
{
  int res;
  assert(!result_field);
  res= (*ref)->save_in_field(to, no_conversions);
  null_value= (*ref)->null_value;
  return res;
}


void Item_ref::save_org_in_field(Field *field)
{
  (*ref)->save_org_in_field(field);
}


void Item_ref::make_field(Send_field *field)
{
  (*ref)->make_field(field);
  /* Non-zero in case of a view */
  if (name)
    field->col_name= name;
  if (table_name)
    field->table_name= table_name;
  if (db_name)
    field->db_name= db_name;
}


Item *Item_ref::get_tmp_table_item(Session *session)
{
  if (!result_field)
    return (*ref)->get_tmp_table_item(session);

  Item_field *item= new Item_field(result_field);
  if (item)
  {
    item->table_name= table_name;
    item->db_name= db_name;
  }
  return item;
}


void Item_ref_null_helper::print(String *str, enum_query_type query_type)
{
  str->append(STRING_WITH_LEN("<ref_null_helper>("));
  if (ref)
    (*ref)->print(str, query_type);
  else
    str->append('?');
  str->append(')');
}


double Item_direct_ref::val_real()
{
  double tmp=(*ref)->val_real();
  null_value=(*ref)->null_value;
  return tmp;
}


int64_t Item_direct_ref::val_int()
{
  int64_t tmp=(*ref)->val_int();
  null_value=(*ref)->null_value;
  return tmp;
}


String *Item_direct_ref::val_str(String* tmp)
{
  tmp=(*ref)->val_str(tmp);
  null_value=(*ref)->null_value;
  return tmp;
}


my_decimal *Item_direct_ref::val_decimal(my_decimal *decimal_value)
{
  my_decimal *tmp= (*ref)->val_decimal(decimal_value);
  null_value=(*ref)->null_value;
  return tmp;
}


bool Item_direct_ref::val_bool()
{
  bool tmp= (*ref)->val_bool();
  null_value=(*ref)->null_value;
  return tmp;
}


bool Item_direct_ref::is_null()
{
  return (*ref)->is_null();
}


bool Item_direct_ref::get_date(DRIZZLE_TIME *ltime,uint32_t fuzzydate)
{
  return (null_value=(*ref)->get_date(ltime,fuzzydate));
}

/*
  Prepare referenced outer field then call usual Item_direct_ref::fix_fields

  SYNOPSIS
    Item_outer_ref::fix_fields()
    session         thread handler
    reference   reference on reference where this item stored

  RETURN
    false   OK
    true    Error
*/

bool Item_outer_ref::fix_fields(Session *session, Item **reference)
{
  bool err;
  /* outer_ref->check_cols() will be made in Item_direct_ref::fix_fields */
  if ((*ref) && !(*ref)->fixed && ((*ref)->fix_fields(session, reference)))
    return true;
  err= Item_direct_ref::fix_fields(session, reference);
  if (!outer_ref)
    outer_ref= *ref;
  if ((*ref)->type() == Item::FIELD_ITEM)
    table_name= ((Item_field*)outer_ref)->table_name;
  return err;
}

void Item_outer_ref::fix_after_pullout(st_select_lex *new_parent, Item **ref)
{
  if (depended_from == new_parent)
  {
    *ref= outer_ref;
    outer_ref->fix_after_pullout(new_parent, ref);
  }
}

void Item_ref::fix_after_pullout(st_select_lex *new_parent, Item **)
{
  if (depended_from == new_parent)
  {
    (*ref)->fix_after_pullout(new_parent, ref);
    depended_from= NULL;
  }
}

bool Item_default_value::eq(const Item *item, bool binary_cmp) const
{
  return item->type() == DEFAULT_VALUE_ITEM &&
    ((Item_default_value *)item)->arg->eq(arg, binary_cmp);
}


bool Item_default_value::fix_fields(Session *session, Item **)
{
  Item *real_arg;
  Item_field *field_arg;
  Field *def_field;
  assert(fixed == 0);

  if (!arg)
  {
    fixed= 1;
    return false;
  }
  if (!arg->fixed && arg->fix_fields(session, &arg))
    goto error;


  real_arg= arg->real_item();
  if (real_arg->type() != FIELD_ITEM)
  {
    my_error(ER_NO_DEFAULT_FOR_FIELD, MYF(0), arg->name);
    goto error;
  }

  field_arg= (Item_field *)real_arg;
  if (field_arg->field->flags & NO_DEFAULT_VALUE_FLAG)
  {
    my_error(ER_NO_DEFAULT_FOR_FIELD, MYF(0), field_arg->field->field_name);
    goto error;
  }
  if (!(def_field= (Field*) sql_alloc(field_arg->field->size_of())))
    goto error;
  memcpy(def_field, field_arg->field, field_arg->field->size_of());
  def_field->move_field_offset((my_ptrdiff_t)
                               (def_field->table->s->default_values -
                                def_field->table->record[0]));
  set_field(def_field);
  return false;

error:
  context->process_error(session);
  return true;
}


void Item_default_value::print(String *str, enum_query_type query_type)
{
  if (!arg)
  {
    str->append(STRING_WITH_LEN("default"));
    return;
  }
  str->append(STRING_WITH_LEN("default("));
  arg->print(str, query_type);
  str->append(')');
}


int Item_default_value::save_in_field(Field *field_arg, bool no_conversions)
{
  if (!arg)
  {
    if (field_arg->flags & NO_DEFAULT_VALUE_FLAG)
    {
      if (field_arg->reset())
      {
        my_message(ER_CANT_CREATE_GEOMETRY_OBJECT,
                   ER(ER_CANT_CREATE_GEOMETRY_OBJECT), MYF(0));
        return -1;
      }

      {
        push_warning_printf(field_arg->table->in_use,
                            DRIZZLE_ERROR::WARN_LEVEL_WARN,
                            ER_NO_DEFAULT_FOR_FIELD,
                            ER(ER_NO_DEFAULT_FOR_FIELD),
                            field_arg->field_name);
      }
      return 1;
    }
    field_arg->set_default();
    return 0;
  }
  return Item_field::save_in_field(field_arg, no_conversions);
}


/**
  This method like the walk method traverses the item tree, but at the
  same time it can replace some nodes in the tree.
*/

Item *Item_default_value::transform(Item_transformer transformer, unsigned char *args)
{
  Item *new_item= arg->transform(transformer, args);
  if (!new_item)
    return 0;

  /*
    Session::change_item_tree() should be called only if the tree was
    really transformed, i.e. when a new item has been created.
    Otherwise we'll be allocating a lot of unnecessary memory for
    change records at each execution.
  */
  if (arg != new_item)
    current_session->change_item_tree(&arg, new_item);
  return (this->*transformer)(args);
}


bool Item_insert_value::eq(const Item *item, bool binary_cmp) const
{
  return item->type() == INSERT_VALUE_ITEM &&
    ((Item_default_value *)item)->arg->eq(arg, binary_cmp);
}


bool Item_insert_value::fix_fields(Session *session, Item **)
{
  assert(fixed == 0);
  /* We should only check that arg is in first table */
  if (!arg->fixed)
  {
    bool res;
    TableList *orig_next_table= context->last_name_resolution_table;
    context->last_name_resolution_table= context->first_name_resolution_table;
    res= arg->fix_fields(session, &arg);
    context->last_name_resolution_table= orig_next_table;
    if (res)
      return true;
  }

  if (arg->type() == REF_ITEM)
  {
    Item_ref *ref= (Item_ref *)arg;
    if (ref->ref[0]->type() != FIELD_ITEM)
    {
      my_error(ER_BAD_FIELD_ERROR, MYF(0), "", "VALUES() function");
      return true;
    }
    arg= ref->ref[0];
  }
  /*
    According to our SQL grammar, VALUES() function can reference
    only to a column.
  */
  assert(arg->type() == FIELD_ITEM);

  Item_field *field_arg= (Item_field *)arg;

  if (field_arg->field->table->insert_values)
  {
    Field *def_field= (Field*) sql_alloc(field_arg->field->size_of());
    if (!def_field)
      return true;
    memcpy(def_field, field_arg->field, field_arg->field->size_of());
    def_field->move_field_offset((my_ptrdiff_t)
                                 (def_field->table->insert_values -
                                  def_field->table->record[0]));
    set_field(def_field);
  }
  else
  {
    Field *tmp_field= field_arg->field;
    /* charset doesn't matter here, it's to avoid sigsegv only */
    tmp_field= new Field_null(0, 0, Field::NONE, field_arg->field->field_name,
                          &my_charset_bin);
    if (tmp_field)
    {
      tmp_field->init(field_arg->field->table);
      set_field(tmp_field);
    }
  }
  return false;
}

void Item_insert_value::print(String *str, enum_query_type query_type)
{
  str->append(STRING_WITH_LEN("values("));
  arg->print(str, query_type);
  str->append(')');
}


Item_result item_cmp_type(Item_result a,Item_result b)
{
  if (a == STRING_RESULT && b == STRING_RESULT)
    return STRING_RESULT;
  if (a == INT_RESULT && b == INT_RESULT)
    return INT_RESULT;
  else if (a == ROW_RESULT || b == ROW_RESULT)
    return ROW_RESULT;
  if ((a == INT_RESULT || a == DECIMAL_RESULT) &&
      (b == INT_RESULT || b == DECIMAL_RESULT))
    return DECIMAL_RESULT;
  return REAL_RESULT;
}


void resolve_const_item(Session *session, Item **ref, Item *comp_item)
{
  Item *item= *ref;
  Item *new_item= NULL;
  if (item->basic_const_item())
    return;                                     // Can't be better
  Item_result res_type=item_cmp_type(comp_item->result_type(),
				     item->result_type());
  char *name=item->name;			// Alloced by sql_alloc

  switch (res_type) {
  case STRING_RESULT:
  {
    char buff[MAX_FIELD_WIDTH];
    String tmp(buff,sizeof(buff),&my_charset_bin),*result;
    result=item->val_str(&tmp);
    if (item->null_value)
      new_item= new Item_null(name);
    else
    {
      uint32_t length= result->length();
      char *tmp_str= sql_strmake(result->ptr(), length);
      new_item= new Item_string(name, tmp_str, length, result->charset());
    }
    break;
  }
  case INT_RESULT:
  {
    int64_t result=item->val_int();
    uint32_t length=item->max_length;
    bool null_value=item->null_value;
    new_item= (null_value ? (Item*) new Item_null(name) :
               (Item*) new Item_int(name, result, length));
    break;
  }
  case ROW_RESULT:
  if (item->type() == Item::ROW_ITEM && comp_item->type() == Item::ROW_ITEM)
  {
    /*
      Substitute constants only in Item_rows. Don't affect other Items
      with ROW_RESULT (eg Item_singlerow_subselect).

      For such Items more optimal is to detect if it is constant and replace
      it with Item_row. This would optimize queries like this:
      SELECT * FROM t1 WHERE (a,b) = (SELECT a,b FROM t2 LIMIT 1);
    */
    Item_row *item_row= (Item_row*) item;
    Item_row *comp_item_row= (Item_row*) comp_item;
    uint32_t col;
    new_item= 0;
    /*
      If item and comp_item are both Item_rows and have same number of cols
      then process items in Item_row one by one.
      We can't ignore NULL values here as this item may be used with <=>, in
      which case NULL's are significant.
    */
    assert(item->result_type() == comp_item->result_type());
    assert(item_row->cols() == comp_item_row->cols());
    col= item_row->cols();
    while (col-- > 0)
      resolve_const_item(session, item_row->addr(col),
                         comp_item_row->element_index(col));
    break;
  }
  /* Fallthrough */
  case REAL_RESULT:
  {						// It must REAL_RESULT
    double result= item->val_real();
    uint32_t length=item->max_length,decimals=item->decimals;
    bool null_value=item->null_value;
    new_item= (null_value ? (Item*) new Item_null(name) : (Item*)
               new Item_float(name, result, decimals, length));
    break;
  }
  case DECIMAL_RESULT:
  {
    my_decimal decimal_value;
    my_decimal *result= item->val_decimal(&decimal_value);
    uint32_t length= item->max_length, decimals= item->decimals;
    bool null_value= item->null_value;
    new_item= (null_value ?
               (Item*) new Item_null(name) :
               (Item*) new Item_decimal(name, result, length, decimals));
    break;
  }
  default:
    assert(0);
  }
  if (new_item)
    session->change_item_tree(ref, new_item);
}

/**
  Return true if the value stored in the field is equal to the const
  item.

  We need to use this on the range optimizer because in some cases
  we can't store the value in the field without some precision/character loss.
*/

bool field_is_equal_to_item(Field *field,Item *item)
{

  Item_result res_type=item_cmp_type(field->result_type(),
				     item->result_type());
  if (res_type == STRING_RESULT)
  {
    char item_buff[MAX_FIELD_WIDTH];
    char field_buff[MAX_FIELD_WIDTH];
    String item_tmp(item_buff,sizeof(item_buff),&my_charset_bin),*item_result;
    String field_tmp(field_buff,sizeof(field_buff),&my_charset_bin);
    item_result=item->val_str(&item_tmp);
    if (item->null_value)
      return 1;					// This must be true
    field->val_str(&field_tmp);
    return !stringcmp(&field_tmp,item_result);
  }
  if (res_type == INT_RESULT)
    return 1;					// Both where of type int
  if (res_type == DECIMAL_RESULT)
  {
    my_decimal item_buf, *item_val,
               field_buf, *field_val;
    item_val= item->val_decimal(&item_buf);
    if (item->null_value)
      return 1;					// This must be true
    field_val= field->val_decimal(&field_buf);
    return !my_decimal_cmp(item_val, field_val);
  }
  double result= item->val_real();
  if (item->null_value)
    return 1;
  return result == field->val_real();
}

Item_cache* Item_cache::get_cache(const Item *item)
{
  switch (item->result_type()) {
  case INT_RESULT:
    return new Item_cache_int();
  case REAL_RESULT:
    return new Item_cache_real();
  case DECIMAL_RESULT:
    return new Item_cache_decimal();
  case STRING_RESULT:
    return new Item_cache_str(item);
  case ROW_RESULT:
    return new Item_cache_row();
  default:
    // should never be in real life
    assert(0);
    return 0;
  }
}


void Item_cache::print(String *str, enum_query_type query_type)
{
  str->append(STRING_WITH_LEN("<cache>("));
  if (example)
    example->print(str, query_type);
  else
    Item::print(str, query_type);
  str->append(')');
}


bool Item_cache::eq_def(Field *field)
{
  return cached_field ? cached_field->eq_def (field) : false;
}


void Item_cache_int::store(Item *item)
{
  value= item->val_int_result();
  null_value= item->null_value;
  unsigned_flag= item->unsigned_flag;
}


void Item_cache_int::store(Item *item, int64_t val_arg)
{
  value= val_arg;
  null_value= item->null_value;
  unsigned_flag= item->unsigned_flag;
}


String *Item_cache_int::val_str(String *str)
{
  assert(fixed == 1);
  str->set(value, default_charset());
  return str;
}


my_decimal *Item_cache_int::val_decimal(my_decimal *decimal_val)
{
  assert(fixed == 1);
  int2my_decimal(E_DEC_FATAL_ERROR, value, unsigned_flag, decimal_val);
  return decimal_val;
}


void Item_cache_real::store(Item *item)
{
  value= item->val_result();
  null_value= item->null_value;
}


int64_t Item_cache_real::val_int()
{
  assert(fixed == 1);
  return (int64_t) rint(value);
}


String* Item_cache_real::val_str(String *str)
{
  assert(fixed == 1);
  str->set_real(value, decimals, default_charset());
  return str;
}


my_decimal *Item_cache_real::val_decimal(my_decimal *decimal_val)
{
  assert(fixed == 1);
  double2my_decimal(E_DEC_FATAL_ERROR, value, decimal_val);
  return decimal_val;
}


void Item_cache_decimal::store(Item *item)
{
  my_decimal *val= item->val_decimal_result(&decimal_value);
  if (!(null_value= item->null_value) && val != &decimal_value)
    my_decimal2decimal(val, &decimal_value);
}

double Item_cache_decimal::val_real()
{
  assert(fixed);
  double res;
  my_decimal2double(E_DEC_FATAL_ERROR, &decimal_value, &res);
  return res;
}

int64_t Item_cache_decimal::val_int()
{
  assert(fixed);
  int64_t res;
  my_decimal2int(E_DEC_FATAL_ERROR, &decimal_value, unsigned_flag, &res);
  return res;
}

String* Item_cache_decimal::val_str(String *str)
{
  assert(fixed);
  my_decimal_round(E_DEC_FATAL_ERROR, &decimal_value, decimals, false,
                   &decimal_value);
  my_decimal2string(E_DEC_FATAL_ERROR, &decimal_value, 0, 0, 0, str);
  return str;
}

my_decimal *Item_cache_decimal::val_decimal(my_decimal *)
{
  assert(fixed);
  return &decimal_value;
}


Item_cache_str::Item_cache_str(const Item *item) :
  Item_cache(), value(0),
  is_varbinary(item->type() == FIELD_ITEM &&
               ((const Item_field *) item)->field->type() ==
               DRIZZLE_TYPE_VARCHAR &&
               !((const Item_field *) item)->field->has_charset())
{}

void Item_cache_str::store(Item *item)
{
  value_buff.set(buffer, sizeof(buffer), item->collation.collation);
  value= item->str_result(&value_buff);
  if ((null_value= item->null_value))
    value= 0;
  else if (value != &value_buff)
  {
    /*
      We copy string value to avoid changing value if 'item' is table field
      in queries like following (where t1.c is varchar):
      select a,
             (select a,b,c from t1 where t1.a=t2.a) = ROW(a,2,'a'),
             (select c from t1 where a=t2.a)
        from t2;
    */
    value_buff.copy(*value);
    value= &value_buff;
  }
}

double Item_cache_str::val_real()
{
  assert(fixed == 1);
  int err_not_used;
  char *end_not_used;
  if (value)
    return my_strntod(value->charset(), (char*) value->ptr(),
		      value->length(), &end_not_used, &err_not_used);
  return (double) 0;
}


int64_t Item_cache_str::val_int()
{
  assert(fixed == 1);
  int err;
  if (value)
    return my_strntoll(value->charset(), value->ptr(),
		       value->length(), 10, (char**) 0, &err);
  else
    return (int64_t)0;
}

my_decimal *Item_cache_str::val_decimal(my_decimal *decimal_val)
{
  assert(fixed == 1);
  if (value)
    string2my_decimal(E_DEC_FATAL_ERROR, value, decimal_val);
  else
    decimal_val= 0;
  return decimal_val;
}


int Item_cache_str::save_in_field(Field *field, bool no_conversions)
{
  int res= Item_cache::save_in_field(field, no_conversions);

  return res;
}


/**
  Dummy error processor used by default by Name_resolution_context.

  @note
    do nothing
*/

void dummy_error_processor(Session *, void *)
{}

/**
  Create field for temporary table using type of given item.

  @param session                   Thread handler
  @param item                  Item to create a field for
  @param table                 Temporary table
  @param copy_func             If set and item is a function, store copy of
                               item in this array
  @param modify_item           1 if item->result_field should point to new
                               item. This is relevent for how fill_record()
                               is going to work:
                               If modify_item is 1 then fill_record() will
                               update the record in the original table.
                               If modify_item is 0 then fill_record() will
                               update the temporary table
  @param convert_blob_length   If >0 create a varstring(convert_blob_length)
                               field instead of blob.

  @retval
    0  on error
  @retval
    new_created field
*/

static Field *create_tmp_field_from_item(Session *,
                                         Item *item, Table *table,
                                         Item ***copy_func, bool modify_item,
                                         uint32_t convert_blob_length)
{
  bool maybe_null= item->maybe_null;
  Field *new_field;

  switch (item->result_type()) {
  case REAL_RESULT:
    new_field= new Field_double(item->max_length, maybe_null,
                                item->name, item->decimals, true);
    break;
  case INT_RESULT:
    /*
      Select an integer type with the minimal fit precision.
      MY_INT32_NUM_DECIMAL_DIGITS is sign inclusive, don't consider the sign.
      Values with MY_INT32_NUM_DECIMAL_DIGITS digits may or may not fit into
      Field_long : make them Field_int64_t.
    */
    if (item->max_length >= (MY_INT32_NUM_DECIMAL_DIGITS - 1))
      new_field=new Field_int64_t(item->max_length, maybe_null,
                                   item->name, item->unsigned_flag);
    else
      new_field=new Field_long(item->max_length, maybe_null,
                               item->name, item->unsigned_flag);
    break;
  case STRING_RESULT:
    assert(item->collation.collation);

    enum enum_field_types type;
    /*
      DATE/TIME fields have STRING_RESULT result type.
      To preserve type they needed to be handled separately.
    */
    if ((type= item->field_type()) == DRIZZLE_TYPE_DATETIME ||
        type == DRIZZLE_TYPE_TIME || type == DRIZZLE_TYPE_DATE ||
        type == DRIZZLE_TYPE_TIMESTAMP)
      new_field= item->tmp_table_field_from_field_type(table, 1);
    /*
      Make sure that the blob fits into a Field_varstring which has
      2-byte lenght.
    */
    else if (item->max_length/item->collation.collation->mbmaxlen > 255 &&
             convert_blob_length <= Field_varstring::MAX_SIZE &&
             convert_blob_length)
      new_field= new Field_varstring(convert_blob_length, maybe_null,
                                     item->name, table->s,
                                     item->collation.collation);
    else
      new_field= item->make_string_field(table);
    new_field->set_derivation(item->collation.derivation);
    break;
  case DECIMAL_RESULT:
  {
    uint8_t dec= item->decimals;
    uint8_t intg= ((Item_decimal *) item)->decimal_precision() - dec;
    uint32_t len= item->max_length;

    /*
      Trying to put too many digits overall in a DECIMAL(prec,dec)
      will always throw a warning. We must limit dec to
      DECIMAL_MAX_SCALE however to prevent an assert() later.
    */

    if (dec > 0)
    {
      signed int overflow;

      dec= cmin(dec, (uint8_t)DECIMAL_MAX_SCALE);

      /*
        If the value still overflows the field with the corrected dec,
        we'll throw out decimals rather than integers. This is still
        bad and of course throws a truncation warning.
        +1: for decimal point
      */

      overflow= my_decimal_precision_to_length(intg + dec, dec,
                                               item->unsigned_flag) - len;

      if (overflow > 0)
        dec= cmax(0, dec - overflow);            // too long, discard fract
      else
        len -= item->decimals - dec;            // corrected value fits
    }

    new_field= new Field_new_decimal(len, maybe_null, item->name,
                                     dec, item->unsigned_flag);
    break;
  }
  case ROW_RESULT:
  default:
    // This case should never be choosen
    assert(0);
    new_field= 0;
    break;
  }
  if (new_field)
    new_field->init(table);

  if (copy_func && item->is_result_field())
    *((*copy_func)++) = item;			// Save for copy_funcs
  if (modify_item)
    item->set_result_field(new_field);
  if (item->type() == Item::NULL_ITEM)
    new_field->is_created_from_null_item= true;
  return new_field;
}

Field *create_tmp_field(Session *session, Table *table,Item *item,
                        Item::Type type, Item ***copy_func, Field **from_field,
                        Field **default_field, bool group, bool modify_item,
                        bool, bool make_copy_field,
                        uint32_t convert_blob_length)
{
  Field *result;
  Item::Type orig_type= type;
  Item *orig_item= 0;

  if (type != Item::FIELD_ITEM &&
      item->real_item()->type() == Item::FIELD_ITEM)
  {
    orig_item= item;
    item= item->real_item();
    type= Item::FIELD_ITEM;
  }

  switch (type) {
  case Item::SUM_FUNC_ITEM:
  {
    Item_sum *item_sum=(Item_sum*) item;
    result= item_sum->create_tmp_field(group, table, convert_blob_length);
    if (!result)
      my_error(ER_OUT_OF_RESOURCES, MYF(ME_FATALERROR));
    return result;
  }
  case Item::FIELD_ITEM:
  case Item::DEFAULT_VALUE_ITEM:
  {
    Item_field *field= (Item_field*) item;
    bool orig_modify= modify_item;
    if (orig_type == Item::REF_ITEM)
      modify_item= 0;
    /*
      If item have to be able to store NULLs but underlaid field can't do it,
      create_tmp_field_from_field() can't be used for tmp field creation.
    */
    if (field->maybe_null && !field->field->maybe_null())
    {
      result= create_tmp_field_from_item(session, item, table, NULL,
                                         modify_item, convert_blob_length);
      *from_field= field->field;
      if (result && modify_item)
        field->result_field= result;
    }
    else
      result= create_tmp_field_from_field(session, (*from_field= field->field),
                                          orig_item ? orig_item->name :
                                          item->name,
                                          table,
                                          modify_item ? field :
                                          NULL,
                                          convert_blob_length);
    if (orig_type == Item::REF_ITEM && orig_modify)
      ((Item_ref*)orig_item)->set_result_field(result);
    if (field->field->eq_def(result))
      *default_field= field->field;
    return result;
  }
  /* Fall through */
  case Item::FUNC_ITEM:
    /* Fall through */
  case Item::COND_ITEM:
  case Item::FIELD_AVG_ITEM:
  case Item::FIELD_STD_ITEM:
  case Item::SUBSELECT_ITEM:
    /* The following can only happen with 'CREATE TABLE ... SELECT' */
  case Item::PROC_ITEM:
  case Item::INT_ITEM:
  case Item::REAL_ITEM:
  case Item::DECIMAL_ITEM:
  case Item::STRING_ITEM:
  case Item::REF_ITEM:
  case Item::NULL_ITEM:
  case Item::VARBIN_ITEM:
    if (make_copy_field)
    {
      assert(((Item_result_field*)item)->result_field);
      *from_field= ((Item_result_field*)item)->result_field;
    }
    return create_tmp_field_from_item(session, item, table,
                                      (make_copy_field ? 0 : copy_func),
                                       modify_item, convert_blob_length);
  case Item::TYPE_HOLDER:
    result= ((Item_type_holder *)item)->make_field_by_type(table);
    result->set_derivation(item->collation.derivation);
    return result;
  default:					// Dosen't have to be stored
    return 0;
  }
}


/**
  Wrapper of hide_view_error call for Name_resolution_context error
  processor.

  @note
    hide view underlying tables details in error messages
*/

/*****************************************************************************
** Instantiate templates
*****************************************************************************/

#ifdef HAVE_EXPLICIT_TEMPLATE_INSTANTIATION
template class List<Item>;
template class List_iterator<Item>;
template class List_iterator_fast<Item>;
template class List_iterator_fast<Item_field>;
template class List<List_item>;
#endif
