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

/**
  @file Handling of MySQL SQL variables

  @details
  To add a new variable, one has to do the following:

  - Use one of the 'sys_var... classes from set_var.h or write a specific
    one for the variable type.
  - Define it in the 'variable definition list' in this file.
  - If the variable is thread specific, add it to 'system_variables' struct.
    If not, add it to mysqld.cc and an declaration in 'mysql_priv.h'
  - If the variable should be changed from the command line, add a definition
    of it in the my_option structure list in mysqld.cc
  - Don't forget to initialize new fields in global_system_variables and
    max_system_variables!

  @todo
    Add full support for the variable character_set (for 4.1)

  @note
    Be careful with var->save_result: sys_var::check() only updates
    uint64_t_value; so other members of the union are garbage then; to use
    them you must first assign a value to them (in specific ::check() for
    example).
*/

#include <drizzled/server_includes.h>
#include <mysys/my_getopt.h>
#include <plugin/myisam/myisam.h>
#include <drizzled/error.h>
#include <drizzled/gettext.h>
#include <drizzled/tztime.h>
#include <drizzled/data_home.h>
#include <drizzled/set_var.h>
#include <drizzled/session.h>
#include <drizzled/sql_base.h>
#include <drizzled/lock.h>
#include <drizzled/item/uint.h>
#include <drizzled/item/null.h>
#include <drizzled/item/float.h>
#include <drizzled/plugin.h>

#include "drizzled/registry.h"
#include <map>
#include <algorithm>

using namespace std;
using namespace drizzled;

extern const CHARSET_INFO *character_set_filesystem;
extern size_t my_thread_stack_size;

class sys_var_pluginvar;
static DYNAMIC_ARRAY fixed_show_vars;
static drizzled::Registry<sys_var *> system_variable_hash;
extern char *opt_drizzle_tmpdir;

const char *bool_type_names[]= { "OFF", "ON", NULL };
TYPELIB bool_typelib=
{
  array_elements(bool_type_names)-1, "", bool_type_names, NULL
};

static bool set_option_bit(Session *session, set_var *var);
static bool set_option_autocommit(Session *session, set_var *var);
static int  check_pseudo_thread_id(Session *session, set_var *var);
static int check_tx_isolation(Session *session, set_var *var);
static void fix_tx_isolation(Session *session, enum_var_type type);
static int check_completion_type(Session *session, set_var *var);
static void fix_completion_type(Session *session, enum_var_type type);
static void fix_max_join_size(Session *session, enum_var_type type);
static void fix_session_mem_root(Session *session, enum_var_type type);
static void fix_trans_mem_root(Session *session, enum_var_type type);
static void fix_server_id(Session *session, enum_var_type type);
static uint64_t fix_unsigned(Session *, uint64_t, const struct my_option *);
static bool get_unsigned32(Session *session, set_var *var);
static bool get_unsigned64(Session *session, set_var *var);
bool throw_bounds_warning(Session *session, bool fixed, bool unsignd,
                          const std::string &name, int64_t val);
static unsigned char *get_error_count(Session *session);
static unsigned char *get_warning_count(Session *session);
static unsigned char *get_tmpdir(Session *session);

/*
  Variable definition list

  These are variables that can be set from the command line, in
  alphabetic order.

  The variables are linked into the list. A variable is added to
  it in the constructor (see sys_var class for details).
*/
static sys_var_chain vars = { NULL, NULL };

static sys_var_session_uint64_t
sys_auto_increment_increment(&vars, "auto_increment_increment",
                             &SV::auto_increment_increment);
static sys_var_session_uint64_t
sys_auto_increment_offset(&vars, "auto_increment_offset",
                          &SV::auto_increment_offset);

static sys_var_const_str       sys_basedir(&vars, "basedir", drizzle_home);
static sys_var_session_uint64_t	sys_bulk_insert_buff_size(&vars, "bulk_insert_buffer_size",
                                                          &SV::bulk_insert_buff_size);
static sys_var_session_uint32_t	sys_completion_type(&vars, "completion_type",
                                                    &SV::completion_type,
                                                    check_completion_type,
                                                    fix_completion_type);
static sys_var_collation_sv
sys_collation_server(&vars, "collation_server", &SV::collation_server, &default_charset_info);
static sys_var_const_str       sys_datadir(&vars, "datadir", drizzle_real_data_home);

static sys_var_session_uint64_t	sys_join_buffer_size(&vars, "join_buffer_size",
                                                     &SV::join_buff_size);
static sys_var_key_buffer_size	sys_key_buffer_size(&vars, "key_buffer_size");
static sys_var_key_cache_uint32_t  sys_key_cache_block_size(&vars, "key_cache_block_size",
                                                        offsetof(KEY_CACHE,
                                                                 param_block_size));
static sys_var_key_cache_uint32_t	sys_key_cache_division_limit(&vars, "key_cache_division_limit",
                                                           offsetof(KEY_CACHE,
                                                                    param_division_limit));
static sys_var_key_cache_uint32_t  sys_key_cache_age_threshold(&vars, "key_cache_age_threshold",
                                                           offsetof(KEY_CACHE,
                                                                    param_age_threshold));
static sys_var_session_uint32_t	sys_max_allowed_packet(&vars, "max_allowed_packet",
                                                       &SV::max_allowed_packet);
static sys_var_uint64_t_ptr	sys_max_connect_errors(&vars, "max_connect_errors",
                                               &max_connect_errors);
static sys_var_session_uint64_t	sys_max_error_count(&vars, "max_error_count",
                                                  &SV::max_error_count);
static sys_var_session_uint64_t	sys_max_heap_table_size(&vars, "max_heap_table_size",
                                                        &SV::max_heap_table_size);
static sys_var_session_uint64_t sys_pseudo_thread_id(&vars, "pseudo_thread_id",
                                              &SV::pseudo_thread_id,
                                              0, check_pseudo_thread_id);
static sys_var_session_ha_rows	sys_max_join_size(&vars, "max_join_size",
                                                  &SV::max_join_size,
                                                  fix_max_join_size);
static sys_var_session_uint64_t	sys_max_seeks_for_key(&vars, "max_seeks_for_key",
                                                      &SV::max_seeks_for_key);
static sys_var_session_uint64_t   sys_max_length_for_sort_data(&vars, "max_length_for_sort_data",
                                                               &SV::max_length_for_sort_data);
static sys_var_session_size_t	sys_max_sort_length(&vars, "max_sort_length",
                                                    &SV::max_sort_length);
static sys_var_uint64_t_ptr	sys_max_write_lock_count(&vars, "max_write_lock_count",
                                                 &max_write_lock_count);
static sys_var_session_uint64_t sys_min_examined_row_limit(&vars, "min_examined_row_limit",
                                                           &SV::min_examined_row_limit);

/* these two cannot be static */
static sys_var_session_bool sys_optimizer_prune_level(&vars, "optimizer_prune_level",
                                                      &SV::optimizer_prune_level);
static sys_var_session_uint32_t sys_optimizer_search_depth(&vars, "optimizer_search_depth",
                                                           &SV::optimizer_search_depth);

static sys_var_session_uint64_t sys_preload_buff_size(&vars, "preload_buffer_size",
                                                      &SV::preload_buff_size);
static sys_var_session_uint32_t sys_read_buff_size(&vars, "read_buffer_size",
                                                   &SV::read_buff_size);
static sys_var_session_uint32_t	sys_read_rnd_buff_size(&vars, "read_rnd_buffer_size",
                                                       &SV::read_rnd_buff_size);
static sys_var_session_uint32_t	sys_div_precincrement(&vars, "div_precision_increment",
                                                      &SV::div_precincrement);

static sys_var_session_size_t	sys_range_alloc_block_size(&vars, "range_alloc_block_size",
                                                           &SV::range_alloc_block_size);
static sys_var_session_uint32_t	sys_query_alloc_block_size(&vars, "query_alloc_block_size",
                                                           &SV::query_alloc_block_size,
                                                           false, fix_session_mem_root);
static sys_var_session_uint32_t	sys_query_prealloc_size(&vars, "query_prealloc_size",
                                                        &SV::query_prealloc_size,
                                                        false, fix_session_mem_root);
static sys_var_readonly sys_tmpdir(&vars, "tmpdir", OPT_GLOBAL, SHOW_CHAR, get_tmpdir);
static sys_var_session_uint32_t	sys_trans_alloc_block_size(&vars, "transaction_alloc_block_size",
                                                           &SV::trans_alloc_block_size,
                                                           false, fix_trans_mem_root);
static sys_var_session_uint32_t	sys_trans_prealloc_size(&vars, "transaction_prealloc_size",
                                                        &SV::trans_prealloc_size,
                                                        false, fix_session_mem_root);

static sys_var_const_str_ptr sys_secure_file_priv(&vars, "secure_file_priv",
                                             &opt_secure_file_priv);
static sys_var_uint32_t_ptr  sys_server_id(&vars, "server_id", &server_id,
                                           fix_server_id);

static sys_var_session_size_t	sys_sort_buffer(&vars, "sort_buffer_size",
                                                &SV::sortbuff_size);
static sys_var_session_optimizer_switch   sys_optimizer_switch(&vars, "optimizer_switch",
                                                               &SV::optimizer_switch);

static sys_var_session_storage_engine sys_storage_engine(&vars, "storage_engine",
				       &SV::storage_engine);
static sys_var_const_str	sys_system_time_zone(&vars, "system_time_zone",
                                             system_time_zone);
static sys_var_uint64_t_ptr	sys_table_def_size(&vars, "table_definition_cache",
                                           &table_def_size);
static sys_var_uint64_t_ptr	sys_table_cache_size(&vars, "table_open_cache",
					     &table_cache_size);
static sys_var_uint64_t_ptr	sys_table_lock_wait_timeout(&vars, "table_lock_wait_timeout",
                                                    &table_lock_wait_timeout);
static sys_var_session_enum	sys_tx_isolation(&vars, "tx_isolation",
                                             &SV::tx_isolation,
                                             &tx_isolation_typelib,
                                             fix_tx_isolation,
                                             check_tx_isolation);
static sys_var_session_uint64_t	sys_tmp_table_size(&vars, "tmp_table_size",
					   &SV::tmp_table_size);
static sys_var_bool_ptr  sys_timed_mutexes(&vars, "timed_mutexes", &timed_mutexes);
static sys_var_const_str	sys_version(&vars, "version", VERSION);
static sys_var_const_str	sys_version_comment(&vars, "version_comment",
                                            COMPILATION_COMMENT);
static sys_var_const_str	sys_version_compile_machine(&vars, "version_compile_machine",
                                                      HOST_CPU);
static sys_var_const_str	sys_version_compile_os(&vars, "version_compile_os",
                                                 HOST_OS);
static sys_var_const_str	sys_version_compile_vendor(&vars, "version_compile_vendor",
                                                 HOST_VENDOR);

/* Variables that are bits in Session */

sys_var_session_bit sys_autocommit(&vars, "autocommit", 0,
                               set_option_autocommit,
                               OPTION_NOT_AUTOCOMMIT,
                               1);
static sys_var_session_bit	sys_big_selects(&vars, "sql_big_selects", 0,
					set_option_bit,
					OPTION_BIG_SELECTS);
static sys_var_session_bit	sys_sql_warnings(&vars, "sql_warnings", 0,
					 set_option_bit,
					 OPTION_WARNINGS);
static sys_var_session_bit	sys_sql_notes(&vars, "sql_notes", 0,
					 set_option_bit,
					 OPTION_SQL_NOTES);
static sys_var_session_bit	sys_safe_updates(&vars, "sql_safe_updates", 0,
					 set_option_bit,
					 OPTION_SAFE_UPDATES);
static sys_var_session_bit	sys_buffer_results(&vars, "sql_buffer_result", 0,
					   set_option_bit,
					   OPTION_BUFFER_RESULT);
static sys_var_session_bit	sys_foreign_key_checks(&vars, "foreign_key_checks", 0,
					       set_option_bit,
					       OPTION_NO_FOREIGN_KEY_CHECKS, 1);
static sys_var_session_bit	sys_unique_checks(&vars, "unique_checks", 0,
					  set_option_bit,
					  OPTION_RELAXED_UNIQUE_CHECKS, 1);
/* Local state variables */

static sys_var_session_ha_rows	sys_select_limit(&vars, "sql_select_limit",
						 &SV::select_limit);
static sys_var_timestamp sys_timestamp(&vars, "timestamp");
static sys_var_last_insert_id
sys_last_insert_id(&vars, "last_insert_id");
/*
  identity is an alias for last_insert_id(), so that we are compatible
  with Sybase
*/
static sys_var_last_insert_id sys_identity(&vars, "identity");

static sys_var_session_lc_time_names sys_lc_time_names(&vars, "lc_time_names");

/*
  We want statements referring explicitly to @@session.insert_id to be
  unsafe, because insert_id is modified internally by the slave sql
  thread when NULL values are inserted in an AUTO_INCREMENT column.
  This modification interfers with the value of the
  @@session.insert_id variable if @@session.insert_id is referred
  explicitly by an insert statement (as is seen by executing "SET
  @@session.insert_id=0; CREATE TABLE t (a INT, b INT KEY
  AUTO_INCREMENT); INSERT INTO t(a) VALUES (@@session.insert_id);" in
  statement-based logging mode: t will be different on master and
  slave).
*/
static sys_var_readonly sys_error_count(&vars, "error_count",
                                        OPT_SESSION,
                                        SHOW_INT,
                                        get_error_count);
static sys_var_readonly sys_warning_count(&vars, "warning_count",
                                          OPT_SESSION,
                                          SHOW_INT,
                                          get_warning_count);

sys_var_session_uint64_t sys_group_concat_max_len(&vars, "group_concat_max_len",
                                                  &SV::group_concat_max_len);

sys_var_session_time_zone sys_time_zone(&vars, "time_zone");

/* Global read-only variable containing hostname */
static sys_var_const_str        sys_hostname(&vars, "hostname", glob_hostname);

/* Read only variables */

static sys_var_have_variable sys_have_symlink(&vars, "have_symlink", &have_symlink);
/*
  Additional variables (not derived from sys_var class, not accessible as
  @@varname in SELECT or SET). Sorted in alphabetical order to facilitate
  maintenance - SHOW VARIABLES will sort its output.
  TODO: remove this list completely
*/

#define FIXED_VARS_SIZE (sizeof(fixed_vars) / sizeof(SHOW_VAR))
static SHOW_VAR fixed_vars[]= {
  {"back_log",                (char*) &back_log,                    SHOW_INT},
  {"language",                language,                             SHOW_CHAR},
#ifdef HAVE_MLOCKALL
  {"locked_in_memory",	      (char*) &locked_in_memory,	    SHOW_MY_BOOL},
#endif
  {"pid_file",                (char*) pidfile_name,                 SHOW_CHAR},
  {"plugin_dir",              (char*) opt_plugin_dir,               SHOW_CHAR},
  {"port",                    (char*) &drizzled_tcp_port,           SHOW_INT},
  {"thread_stack",            (char*) &my_thread_stack_size,        SHOW_INT},
};

bool sys_var::check(Session *, set_var *var)
{
  var->save_result.uint64_t_value= var->value->val_int();
  return 0;
}

bool sys_var_str::check(Session *session, set_var *var)
{
  int res;
  if (!check_func)
    return 0;

  if ((res=(*check_func)(session, var)) < 0)
    my_error(ER_WRONG_VALUE_FOR_VAR, MYF(0), getName().c_str(), var->value->str_value.ptr());
  return res;
}

/*
  Functions to check and update variables
*/


/**
  Set the OPTION_BIG_SELECTS flag if max_join_size == HA_POS_ERROR.
*/

static void fix_max_join_size(Session *session, enum_var_type type)
{
  if (type != OPT_GLOBAL)
  {
    if (session->variables.max_join_size == HA_POS_ERROR)
      session->options|= OPTION_BIG_SELECTS;
    else
      session->options&= ~OPTION_BIG_SELECTS;
  }
}


/**
  Can't change the 'next' tx_isolation while we are already in
  a transaction
*/
static int check_tx_isolation(Session *session, set_var *var)
{
  if (var->type == OPT_DEFAULT && (session->server_status & SERVER_STATUS_IN_TRANS))
  {
    my_error(ER_CANT_CHANGE_TX_ISOLATION, MYF(0));
    return 1;
  }
  return 0;
}

/*
  If one doesn't use the SESSION modifier, the isolation level
  is only active for the next command.
*/
static void fix_tx_isolation(Session *session, enum_var_type type)
{
  if (type == OPT_SESSION)
    session->session_tx_isolation= ((enum_tx_isolation)
                                    session->variables.tx_isolation);
}

static void fix_completion_type(Session *, enum_var_type) {}

static int check_completion_type(Session *, set_var *var)
{
  int64_t val= var->value->val_int();
  if (val < 0 || val > 2)
  {
    char buf[64];
    my_error(ER_WRONG_VALUE_FOR_VAR, MYF(0), var->var->getName().c_str(), llstr(val, buf));
    return 1;
  }
  return 0;
}


static void fix_session_mem_root(Session *session, enum_var_type type)
{
  if (type != OPT_GLOBAL)
    reset_root_defaults(session->mem_root,
                        session->variables.query_alloc_block_size,
                        session->variables.query_prealloc_size);
}


static void fix_trans_mem_root(Session *session, enum_var_type type)
{
  if (type != OPT_GLOBAL)
    reset_root_defaults(&session->transaction.mem_root,
                        session->variables.trans_alloc_block_size,
                        session->variables.trans_prealloc_size);
}


static void fix_server_id(Session *, enum_var_type)
{
}


bool throw_bounds_warning(Session *session, bool fixed, bool unsignd,
                          const std::string &name, int64_t val)
{
  if (fixed)
  {
    char buf[22];

    if (unsignd)
      ullstr((uint64_t) val, buf);
    else
      llstr(val, buf);

    push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_ERROR,
                        ER_TRUNCATED_WRONG_VALUE,
                        ER(ER_TRUNCATED_WRONG_VALUE), name.c_str(), buf);
  }
  return false;
}

static uint64_t fix_unsigned(Session *session, uint64_t num,
                              const struct my_option *option_limits)
{
  bool fixed= false;
  uint64_t out= getopt_ull_limit_value(num, option_limits, &fixed);

  throw_bounds_warning(session, fixed, true, option_limits->name, (int64_t) num);
  return out;
}


static size_t fix_size_t(Session *session, size_t num,
                           const struct my_option *option_limits)
{
  bool fixed= false;
  size_t out= (size_t)getopt_ull_limit_value(num, option_limits, &fixed);

  throw_bounds_warning(session, fixed, true, option_limits->name, (int64_t) num);
  return out;
}

static bool get_unsigned32(Session *session, set_var *var)
{
  if (var->value->unsigned_flag)
    var->save_result.uint32_t_value= 
      static_cast<uint32_t>(var->value->val_int());
  else
  {
    int64_t v= var->value->val_int();
    if (v > UINT32_MAX)
      throw_bounds_warning(session, true, true,var->var->getName().c_str(), v);
    
    var->save_result.uint32_t_value= 
      static_cast<uint32_t>((v > UINT32_MAX) ? UINT32_MAX : (v < 0) ? 0 : v);
  }
  return false;
}

static bool get_unsigned64(Session *, set_var *var)
{
  if (var->value->unsigned_flag)
      var->save_result.uint64_t_value=(uint64_t) var->value->val_int();
  else
  {
    int64_t v= var->value->val_int();
      var->save_result.uint64_t_value= (uint64_t) ((v < 0) ? 0 : v);
  }
  return 0;
}

static bool get_size_t(Session *, set_var *var)
{
  if (var->value->unsigned_flag)
    var->save_result.size_t_value= (size_t) var->value->val_int();
  else
  {
    ssize_t v= (ssize_t)var->value->val_int();
    var->save_result.size_t_value= (size_t) ((v < 0) ? 0 : v);
  }
  return 0;
}

bool sys_var_uint32_t_ptr::check(Session *, set_var *var)
{
  var->save_result.uint32_t_value= (uint32_t)var->value->val_int();
  return 0;
}

bool sys_var_uint32_t_ptr::update(Session *session, set_var *var)
{
  uint32_t tmp= var->save_result.uint32_t_value;
  pthread_mutex_lock(&LOCK_global_system_variables);
  if (option_limits)
  {
    uint32_t newvalue= (uint32_t) fix_unsigned(session, tmp, option_limits);
    if(newvalue==tmp)
      *value= newvalue;
  }
  else
    *value= (uint32_t) tmp;
  pthread_mutex_unlock(&LOCK_global_system_variables);
  return 0;
}


void sys_var_uint32_t_ptr::set_default(Session *, enum_var_type)
{
  bool not_used;
  pthread_mutex_lock(&LOCK_global_system_variables);
  *value= (uint32_t)getopt_ull_limit_value((uint32_t) option_limits->def_value,
                                           option_limits, &not_used);
  pthread_mutex_unlock(&LOCK_global_system_variables);
}


bool sys_var_uint64_t_ptr::update(Session *session, set_var *var)
{
  uint64_t tmp= var->save_result.uint64_t_value;
  pthread_mutex_lock(&LOCK_global_system_variables);
  if (option_limits)
  {
    uint64_t newvalue= (uint64_t) fix_unsigned(session, tmp, option_limits);
    if(newvalue==tmp)
      *value= newvalue;
  }
  else
    *value= (uint64_t) tmp;
  pthread_mutex_unlock(&LOCK_global_system_variables);
  return 0;
}


void sys_var_uint64_t_ptr::set_default(Session *, enum_var_type)
{
  bool not_used;
  pthread_mutex_lock(&LOCK_global_system_variables);
  *value= getopt_ull_limit_value((uint64_t) option_limits->def_value,
                                 option_limits, &not_used);
  pthread_mutex_unlock(&LOCK_global_system_variables);
}


bool sys_var_size_t_ptr::update(Session *session, set_var *var)
{
  size_t tmp= var->save_result.size_t_value;
  pthread_mutex_lock(&LOCK_global_system_variables);
  if (option_limits)
    *value= fix_size_t(session, tmp, option_limits);
  else
    *value= tmp;
  pthread_mutex_unlock(&LOCK_global_system_variables);
  return 0;
}


void sys_var_size_t_ptr::set_default(Session *, enum_var_type)
{
  bool not_used;
  pthread_mutex_lock(&LOCK_global_system_variables);
  *value= (size_t)getopt_ull_limit_value((size_t) option_limits->def_value,
                                         option_limits, &not_used);
  pthread_mutex_unlock(&LOCK_global_system_variables);
}

bool sys_var_bool_ptr::update(Session *, set_var *var)
{
  *value= (bool) var->save_result.uint32_t_value;
  return 0;
}


void sys_var_bool_ptr::set_default(Session *, enum_var_type)
{
  *value= (bool) option_limits->def_value;
}


bool sys_var_enum::update(Session *, set_var *var)
{
  *value= (uint32_t) var->save_result.uint32_t_value;
  return 0;
}


unsigned char *sys_var_enum::value_ptr(Session *, enum_var_type, const LEX_STRING *)
{
  return (unsigned char*) enum_names->type_names[*value];
}


unsigned char *sys_var_enum_const::value_ptr(Session *, enum_var_type,
                                             const LEX_STRING *)
{
  return (unsigned char*) enum_names->type_names[global_system_variables.*offset];
}

/*
  32 bit types for session variables
*/
bool sys_var_session_uint32_t::check(Session *session, set_var *var)
{
  return (get_unsigned32(session, var) ||
          (check_func && (*check_func)(session, var)));
}

bool sys_var_session_uint32_t::update(Session *session, set_var *var)
{
  uint64_t tmp= (uint64_t) var->save_result.uint32_t_value;

  /* Don't use bigger value than given with --maximum-variable-name=.. */
  if ((uint32_t) tmp > max_system_variables.*offset)
  {
    throw_bounds_warning(session, true, true, getName(), (int64_t) tmp);
    tmp= max_system_variables.*offset;
  }

  if (option_limits)
    tmp= (uint32_t) fix_unsigned(session, tmp, option_limits);
  else if (tmp > UINT32_MAX)
  {
    tmp= UINT32_MAX;
    throw_bounds_warning(session, true, true, getName(), (int64_t) var->save_result.uint64_t_value);
  }

  if (var->type == OPT_GLOBAL)
     global_system_variables.*offset= (uint32_t) tmp;
   else
     session->variables.*offset= (uint32_t) tmp;

   return 0;
 }


 void sys_var_session_uint32_t::set_default(Session *session, enum_var_type type)
 {
   if (type == OPT_GLOBAL)
   {
     bool not_used;
     /* We will not come here if option_limits is not set */
     global_system_variables.*offset=
       (uint32_t) getopt_ull_limit_value((uint32_t) option_limits->def_value,
                                      option_limits, &not_used);
   }
   else
     session->variables.*offset= global_system_variables.*offset;
 }


unsigned char *sys_var_session_uint32_t::value_ptr(Session *session,
                                                enum_var_type type,
                                                const LEX_STRING *)
{
  if (type == OPT_GLOBAL)
    return (unsigned char*) &(global_system_variables.*offset);
  return (unsigned char*) &(session->variables.*offset);
}


bool sys_var_session_ha_rows::update(Session *session, set_var *var)
{
  uint64_t tmp= var->save_result.uint64_t_value;

  /* Don't use bigger value than given with --maximum-variable-name=.. */
  if ((ha_rows) tmp > max_system_variables.*offset)
    tmp= max_system_variables.*offset;

  if (option_limits)
    tmp= (ha_rows) fix_unsigned(session, tmp, option_limits);
  if (var->type == OPT_GLOBAL)
  {
    /* Lock is needed to make things safe on 32 bit systems */
    pthread_mutex_lock(&LOCK_global_system_variables);
    global_system_variables.*offset= (ha_rows) tmp;
    pthread_mutex_unlock(&LOCK_global_system_variables);
  }
  else
    session->variables.*offset= (ha_rows) tmp;
  return 0;
}


void sys_var_session_ha_rows::set_default(Session *session, enum_var_type type)
{
  if (type == OPT_GLOBAL)
  {
    bool not_used;
    /* We will not come here if option_limits is not set */
    pthread_mutex_lock(&LOCK_global_system_variables);
    global_system_variables.*offset=
      (ha_rows) getopt_ull_limit_value((ha_rows) option_limits->def_value,
                                       option_limits, &not_used);
    pthread_mutex_unlock(&LOCK_global_system_variables);
  }
  else
    session->variables.*offset= global_system_variables.*offset;
}


unsigned char *sys_var_session_ha_rows::value_ptr(Session *session,
                                                  enum_var_type type,
                                                  const LEX_STRING *)
{
  if (type == OPT_GLOBAL)
    return (unsigned char*) &(global_system_variables.*offset);
  return (unsigned char*) &(session->variables.*offset);
}

bool sys_var_session_uint64_t::check(Session *session, set_var *var)
{
  return (get_unsigned64(session, var) ||
	  (check_func && (*check_func)(session, var)));
}

bool sys_var_session_uint64_t::update(Session *session,  set_var *var)
{
  uint64_t tmp= var->save_result.uint64_t_value;

  if (tmp > max_system_variables.*offset)
  {
    throw_bounds_warning(session, true, true, getName(), (int64_t) tmp);
    tmp= max_system_variables.*offset;
  }

  if (option_limits)
    tmp= fix_unsigned(session, tmp, option_limits);
  if (var->type == OPT_GLOBAL)
  {
    /* Lock is needed to make things safe on 32 bit systems */
    pthread_mutex_lock(&LOCK_global_system_variables);
    global_system_variables.*offset= (uint64_t) tmp;
    pthread_mutex_unlock(&LOCK_global_system_variables);
  }
  else
    session->variables.*offset= (uint64_t) tmp;
  return 0;
}


void sys_var_session_uint64_t::set_default(Session *session, enum_var_type type)
{
  if (type == OPT_GLOBAL)
  {
    bool not_used;
    pthread_mutex_lock(&LOCK_global_system_variables);
    global_system_variables.*offset=
      getopt_ull_limit_value((uint64_t) option_limits->def_value,
                             option_limits, &not_used);
    pthread_mutex_unlock(&LOCK_global_system_variables);
  }
  else
    session->variables.*offset= global_system_variables.*offset;
}


unsigned char *sys_var_session_uint64_t::value_ptr(Session *session,
                                                   enum_var_type type,
                                                   const LEX_STRING *)
{
  if (type == OPT_GLOBAL)
    return (unsigned char*) &(global_system_variables.*offset);
  return (unsigned char*) &(session->variables.*offset);
}

bool sys_var_session_size_t::check(Session *session, set_var *var)
{
  return (get_size_t(session, var) ||
	  (check_func && (*check_func)(session, var)));
}

bool sys_var_session_size_t::update(Session *session,  set_var *var)
{
  size_t tmp= var->save_result.size_t_value;

  if (tmp > max_system_variables.*offset)
    tmp= max_system_variables.*offset;

  if (option_limits)
    tmp= fix_size_t(session, tmp, option_limits);
  if (var->type == OPT_GLOBAL)
  {
    /* Lock is needed to make things safe on 32 bit systems */
    pthread_mutex_lock(&LOCK_global_system_variables);
    global_system_variables.*offset= tmp;
    pthread_mutex_unlock(&LOCK_global_system_variables);
  }
  else
    session->variables.*offset= tmp;
  return 0;
}


void sys_var_session_size_t::set_default(Session *session, enum_var_type type)
{
  if (type == OPT_GLOBAL)
  {
    bool not_used;
    pthread_mutex_lock(&LOCK_global_system_variables);
    global_system_variables.*offset=
      (size_t)getopt_ull_limit_value((size_t) option_limits->def_value,
                                     option_limits, &not_used);
    pthread_mutex_unlock(&LOCK_global_system_variables);
  }
  else
    session->variables.*offset= global_system_variables.*offset;
}


unsigned char *sys_var_session_size_t::value_ptr(Session *session,
                                                 enum_var_type type,
                                                 const LEX_STRING *)
{
  if (type == OPT_GLOBAL)
    return (unsigned char*) &(global_system_variables.*offset);
  return (unsigned char*) &(session->variables.*offset);
}


bool sys_var_session_bool::update(Session *session,  set_var *var)
{
  if (var->type == OPT_GLOBAL)
    global_system_variables.*offset= (bool) var->save_result.uint32_t_value;
  else
    session->variables.*offset= (bool) var->save_result.uint32_t_value;
  return 0;
}


void sys_var_session_bool::set_default(Session *session,  enum_var_type type)
{
  if (type == OPT_GLOBAL)
    global_system_variables.*offset= (bool) option_limits->def_value;
  else
    session->variables.*offset= global_system_variables.*offset;
}


unsigned char *sys_var_session_bool::value_ptr(Session *session,
                                               enum_var_type type,
                                               const LEX_STRING *)
{
  if (type == OPT_GLOBAL)
    return (unsigned char*) &(global_system_variables.*offset);
  return (unsigned char*) &(session->variables.*offset);
}


bool sys_var::check_enum(Session *,
                         set_var *var, const TYPELIB *enum_names)
{
  char buff[STRING_BUFFER_USUAL_SIZE];
  const char *value;
  String str(buff, sizeof(buff), system_charset_info), *res;

  if (var->value->result_type() == STRING_RESULT)
  {
    if (!(res=var->value->val_str(&str)) ||
        (var->save_result.uint32_t_value= find_type(enum_names, res->ptr(),
                                                    res->length(),1)) == 0)
    {
      value= res ? res->c_ptr() : "NULL";
      goto err;
    }

    var->save_result.uint32_t_value--;
  }
  else
  {
    uint64_t tmp=var->value->val_int();
    if (tmp >= enum_names->count)
    {
      llstr(tmp,buff);
      value=buff;				// Wrong value is here
      goto err;
    }
    var->save_result.uint32_t_value= (uint32_t) tmp;	// Save for update
  }
  return 0;

err:
  my_error(ER_WRONG_VALUE_FOR_VAR, MYF(0), name.c_str(), value);
  return 1;
}


bool sys_var::check_set(Session *, set_var *var, TYPELIB *enum_names)
{
  bool not_used;
  char buff[STRING_BUFFER_USUAL_SIZE], *error= 0;
  uint32_t error_len= 0;
  String str(buff, sizeof(buff), system_charset_info), *res;

  if (var->value->result_type() == STRING_RESULT)
  {
    if (!(res= var->value->val_str(&str)))
    {
      strcpy(buff, "NULL");
      goto err;
    }

    if (! m_allow_empty_value && res->length() == 0)
    {
      buff[0]= 0;
      goto err;
    }

    var->save_result.uint32_t_value= ((uint32_t)
                                      find_set(enum_names, res->c_ptr(),
                                               res->length(),
                                               NULL,
                                               &error, &error_len,
                                               &not_used));
    if (error_len)
    {
      size_t len = min((uint32_t)(sizeof(buff) - 1), error_len);
      strncpy(buff, error, len);
      buff[len]= '\0';
      goto err;
    }
  }
  else
  {
    uint64_t tmp= var->value->val_int();

    if (! m_allow_empty_value && tmp == 0)
    {
      buff[0]= '0';
      buff[1]= 0;
      goto err;
    }

    /*
      For when the enum is made to contain 64 elements, as 1ULL<<64 is
      undefined, we guard with a "count<64" test.
    */
    if (unlikely((tmp >= ((1UL) << enum_names->count)) &&
                 (enum_names->count < 64)))
    {
      llstr(tmp, buff);
      goto err;
    }
    var->save_result.uint32_t_value= (uint32_t) tmp;  // Save for update
  }
  return 0;

err:
  my_error(ER_WRONG_VALUE_FOR_VAR, MYF(0), name.c_str(), buff);
  return 1;
}


/**
  Return an Item for a variable.

  Used with @@[global.]variable_name.

  If type is not given, return local value if exists, else global.
*/

Item *sys_var::item(Session *session, enum_var_type var_type, const LEX_STRING *base)
{
  if (check_type(var_type))
  {
    if (var_type != OPT_DEFAULT)
    {
      my_error(ER_INCORRECT_GLOBAL_LOCAL_VAR, MYF(0),
               name.c_str(), var_type == OPT_GLOBAL ? "SESSION" : "GLOBAL");
      return 0;
    }
    /* As there was no local variable, return the global value */
    var_type= OPT_GLOBAL;
  }
  switch (show_type()) {
  case SHOW_LONG:
  case SHOW_INT:
  {
    uint32_t value;
    pthread_mutex_lock(&LOCK_global_system_variables);
    value= *(uint*) value_ptr(session, var_type, base);
    pthread_mutex_unlock(&LOCK_global_system_variables);
    return new Item_uint((uint64_t) value);
  }
  case SHOW_LONGLONG:
  {
    int64_t value;
    pthread_mutex_lock(&LOCK_global_system_variables);
    value= *(int64_t*) value_ptr(session, var_type, base);
    pthread_mutex_unlock(&LOCK_global_system_variables);
    return new Item_int(value);
  }
  case SHOW_DOUBLE:
  {
    double value;
    pthread_mutex_lock(&LOCK_global_system_variables);
    value= *(double*) value_ptr(session, var_type, base);
    pthread_mutex_unlock(&LOCK_global_system_variables);
    /* 6, as this is for now only used with microseconds */
    return new Item_float(value, 6);
  }
  case SHOW_HA_ROWS:
  {
    ha_rows value;
    pthread_mutex_lock(&LOCK_global_system_variables);
    value= *(ha_rows*) value_ptr(session, var_type, base);
    pthread_mutex_unlock(&LOCK_global_system_variables);
    return new Item_int((uint64_t) value);
  }
  case SHOW_SIZE:
  {
    size_t value;
    pthread_mutex_lock(&LOCK_global_system_variables);
    value= *(size_t*) value_ptr(session, var_type, base);
    pthread_mutex_unlock(&LOCK_global_system_variables);
    return new Item_int((uint64_t) value);
  }
  case SHOW_MY_BOOL:
  {
    int32_t value;
    pthread_mutex_lock(&LOCK_global_system_variables);
    value= *(bool*) value_ptr(session, var_type, base);
    pthread_mutex_unlock(&LOCK_global_system_variables);
    return new Item_int(value,1);
  }
  case SHOW_CHAR_PTR:
  {
    Item *tmp;
    pthread_mutex_lock(&LOCK_global_system_variables);
    char *str= *(char**) value_ptr(session, var_type, base);
    if (str)
    {
      uint32_t length= strlen(str);
      tmp= new Item_string(session->strmake(str, length), length,
                           system_charset_info, DERIVATION_SYSCONST);
    }
    else
    {
      tmp= new Item_null();
      tmp->collation.set(system_charset_info, DERIVATION_SYSCONST);
    }
    pthread_mutex_unlock(&LOCK_global_system_variables);
    return tmp;
  }
  case SHOW_CHAR:
  {
    Item *tmp;
    pthread_mutex_lock(&LOCK_global_system_variables);
    char *str= (char*) value_ptr(session, var_type, base);
    if (str)
      tmp= new Item_string(str, strlen(str),
                           system_charset_info, DERIVATION_SYSCONST);
    else
    {
      tmp= new Item_null();
      tmp->collation.set(system_charset_info, DERIVATION_SYSCONST);
    }
    pthread_mutex_unlock(&LOCK_global_system_variables);
    return tmp;
  }
  default:
    my_error(ER_VAR_CANT_BE_READ, MYF(0), name.c_str());
  }
  return 0;
}


bool sys_var_session_enum::update(Session *session, set_var *var)
{
  if (var->type == OPT_GLOBAL)
    global_system_variables.*offset= var->save_result.uint32_t_value;
  else
    session->variables.*offset= var->save_result.uint32_t_value;
  return 0;
}


void sys_var_session_enum::set_default(Session *session, enum_var_type type)
{
  if (type == OPT_GLOBAL)
    global_system_variables.*offset= (uint32_t) option_limits->def_value;
  else
    session->variables.*offset= global_system_variables.*offset;
}


unsigned char *sys_var_session_enum::value_ptr(Session *session,
                                               enum_var_type type,
                                               const LEX_STRING *)
{
  uint32_t tmp= ((type == OPT_GLOBAL) ?
	      global_system_variables.*offset :
	      session->variables.*offset);
  return (unsigned char*) enum_names->type_names[tmp];
}

bool sys_var_session_bit::check(Session *session, set_var *var)
{
  return (check_enum(session, var, &bool_typelib) ||
          (check_func && (*check_func)(session, var)));
}

bool sys_var_session_bit::update(Session *session, set_var *var)
{
  int res= (*update_func)(session, var);
  return res;
}


unsigned char *sys_var_session_bit::value_ptr(Session *session, enum_var_type,
                                              const LEX_STRING *)
{
  /*
    If reverse is 0 (default) return 1 if bit is set.
    If reverse is 1, return 0 if bit is set
  */
  session->sys_var_tmp.bool_value= ((session->options & bit_flag) ?
				   !reverse : reverse);
  return (unsigned char*) &session->sys_var_tmp.bool_value;
}


typedef struct old_names_map_st
{
  const char *old_name;
  const char *new_name;
} my_old_conv;

bool sys_var_collation::check(Session *, set_var *var)
{
  const CHARSET_INFO *tmp;

  if (var->value->result_type() == STRING_RESULT)
  {
    char buff[STRING_BUFFER_USUAL_SIZE];
    String str(buff,sizeof(buff), system_charset_info), *res;
    if (!(res=var->value->val_str(&str)))
    {
      my_error(ER_WRONG_VALUE_FOR_VAR, MYF(0), name.c_str(), "NULL");
      return 1;
    }
    if (!(tmp=get_charset_by_name(res->c_ptr())))
    {
      my_error(ER_UNKNOWN_COLLATION, MYF(0), res->c_ptr());
      return 1;
    }
  }
  else // INT_RESULT
  {
    if (!(tmp=get_charset((int) var->value->val_int())))
    {
      char buf[20];
      int10_to_str((int) var->value->val_int(), buf, -10);
      my_error(ER_UNKNOWN_COLLATION, MYF(0), buf);
      return 1;
    }
  }
  var->save_result.charset= tmp;	// Save for update
  return 0;
}


bool sys_var_collation_sv::update(Session *session, set_var *var)
{
  if (var->type == OPT_GLOBAL)
    global_system_variables.*offset= var->save_result.charset;
  else
  {
    session->variables.*offset= var->save_result.charset;
  }
  return 0;
}


void sys_var_collation_sv::set_default(Session *session, enum_var_type type)
{
  if (type == OPT_GLOBAL)
    global_system_variables.*offset= *global_default;
  else
  {
    session->variables.*offset= global_system_variables.*offset;
  }
}


unsigned char *sys_var_collation_sv::value_ptr(Session *session,
                                               enum_var_type type,
                                               const LEX_STRING *)
{
  const CHARSET_INFO *cs= ((type == OPT_GLOBAL) ?
                           global_system_variables.*offset :
                           session->variables.*offset);
  return cs ? (unsigned char*) cs->name : (unsigned char*) "NULL";
}


unsigned char *sys_var_key_cache_param::value_ptr(Session *, enum_var_type,
                                                  const LEX_STRING *)
{
  return (unsigned char*) dflt_key_cache + offset ;
}

/**
  Resize key cache.
*/
static int resize_key_cache_with_lock(KEY_CACHE *key_cache)
{
  assert(key_cache->key_cache_inited);

  pthread_mutex_lock(&LOCK_global_system_variables);
  long tmp_buff_size= (long) key_cache->param_buff_size;
  long tmp_block_size= (long) key_cache->param_block_size;
  uint32_t division_limit= key_cache->param_division_limit;
  uint32_t age_threshold=  key_cache->param_age_threshold;
  pthread_mutex_unlock(&LOCK_global_system_variables);

  return(!resize_key_cache(key_cache, tmp_block_size,
                           tmp_buff_size,
                           division_limit, age_threshold));
}



bool sys_var_key_buffer_size::update(Session *session, set_var *var)
{
  uint64_t tmp= var->save_result.uint64_t_value;
  KEY_CACHE *key_cache;
  bool error= 0;

  pthread_mutex_lock(&LOCK_global_system_variables);
  key_cache= dflt_key_cache;

  /*
    Abort if some other thread is changing the key cache
    TODO: This should be changed so that we wait until the previous
    assignment is done and then do the new assign
  */
  if (key_cache->in_init)
    goto end;

  if (tmp == 0)					// Zero size means delete
  {
    push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                        ER_WARN_CANT_DROP_DEFAULT_KEYCACHE,
                        ER(ER_WARN_CANT_DROP_DEFAULT_KEYCACHE));
    goto end;					// Ignore default key cache
  }

  key_cache->param_buff_size=
    (uint64_t) fix_unsigned(session, tmp, option_limits);

  /* If key cache didn't existed initialize it, else resize it */
  key_cache->in_init= 1;
  pthread_mutex_unlock(&LOCK_global_system_variables);

  error= (bool)(resize_key_cache_with_lock(key_cache));

  pthread_mutex_lock(&LOCK_global_system_variables);
  key_cache->in_init= 0;

end:
  pthread_mutex_unlock(&LOCK_global_system_variables);
  return error;
}


/**
  @todo
  Abort if some other thread is changing the key cache.
  This should be changed so that we wait until the previous
  assignment is done and then do the new assign
*/
bool sys_var_key_cache_uint32_t::update(Session *session, set_var *var)
{
  uint64_t tmp= (uint64_t) var->value->val_int();
  bool error= 0;

  pthread_mutex_lock(&LOCK_global_system_variables);

  /*
    Abort if some other thread is changing the key cache
    TODO: This should be changed so that we wait until the previous
    assignment is done and then do the new assign
  */
  if (dflt_key_cache->in_init)
    goto end;

  *((uint32_t*) (((char*) dflt_key_cache) + offset))=
    (uint32_t) fix_unsigned(session, tmp, option_limits);

  /*
    Don't create a new key cache if it didn't exist
    (key_caches are created only when the user sets block_size)
  */
  dflt_key_cache->in_init= 1;

  pthread_mutex_unlock(&LOCK_global_system_variables);

  error= (bool) (resize_key_cache_with_lock(dflt_key_cache));

  pthread_mutex_lock(&LOCK_global_system_variables);
  dflt_key_cache->in_init= 0;

end:
  pthread_mutex_unlock(&LOCK_global_system_variables);
  return error;
}


/****************************************************************************/

bool sys_var_timestamp::update(Session *session,  set_var *var)
{
  session->set_time((time_t) var->save_result.uint64_t_value);
  return 0;
}


void sys_var_timestamp::set_default(Session *session, enum_var_type)
{
  session->user_time=0;
}


unsigned char *sys_var_timestamp::value_ptr(Session *session, enum_var_type,
                                            const LEX_STRING *)
{
  session->sys_var_tmp.int32_t_value= (int32_t) session->start_time;
  return (unsigned char*) &session->sys_var_tmp.int32_t_value;
}


bool sys_var_last_insert_id::update(Session *session, set_var *var)
{
  session->first_successful_insert_id_in_prev_stmt=
    var->save_result.uint64_t_value;
  return 0;
}


unsigned char *sys_var_last_insert_id::value_ptr(Session *session,
                                                 enum_var_type,
                                                 const LEX_STRING *)
{
  /*
    this tmp var makes it robust againt change of type of
    read_first_successful_insert_id_in_prev_stmt().
  */
  session->sys_var_tmp.uint64_t_value=
    session->read_first_successful_insert_id_in_prev_stmt();
  return (unsigned char*) &session->sys_var_tmp.uint64_t_value;
}


bool sys_var_session_time_zone::check(Session *session, set_var *var)
{
  char buff[MAX_TIME_ZONE_NAME_LENGTH];
  String str(buff, sizeof(buff), &my_charset_utf8_general_ci);
  String *res= var->value->val_str(&str);

  if (!(var->save_result.time_zone= my_tz_find(session, res)))
  {
    my_error(ER_UNKNOWN_TIME_ZONE, MYF(0), res ? res->c_ptr() : "NULL");
    return 1;
  }
  return 0;
}


bool sys_var_session_time_zone::update(Session *session, set_var *var)
{
  /* We are using Time_zone object found during check() phase. */
  if (var->type == OPT_GLOBAL)
  {
    pthread_mutex_lock(&LOCK_global_system_variables);
    global_system_variables.time_zone= var->save_result.time_zone;
    pthread_mutex_unlock(&LOCK_global_system_variables);
  }
  else
    session->variables.time_zone= var->save_result.time_zone;
  return 0;
}


unsigned char *sys_var_session_time_zone::value_ptr(Session *session,
                                                    enum_var_type type,
                                                    const LEX_STRING *)
{
  /*
    We can use ptr() instead of c_ptr() here because String contaning
    time zone name is guaranteed to be zero ended.
  */
  if (type == OPT_GLOBAL)
    return (unsigned char *)(global_system_variables.time_zone->get_name()->ptr());
  else
  {
    /*
      This is an ugly fix for replication: we don't replicate properly queries
      invoking system variables' values to update tables; but
      CONVERT_TZ(,,@@session.time_zone) is so popular that we make it
      replicable (i.e. we tell the binlog code to store the session
      timezone). If it's the global value which was used we can't replicate
      (binlog code stores session value only).
    */
    return (unsigned char *)(session->variables.time_zone->get_name()->ptr());
  }
}


void sys_var_session_time_zone::set_default(Session *session, enum_var_type type)
{
 pthread_mutex_lock(&LOCK_global_system_variables);
 if (type == OPT_GLOBAL)
 {
   if (default_tz_name)
   {
     String str(default_tz_name, &my_charset_utf8_general_ci);
     /*
       We are guaranteed to find this time zone since its existence
       is checked during start-up.
     */
     global_system_variables.time_zone= my_tz_find(session, &str);
   }
   else
     global_system_variables.time_zone= my_tz_SYSTEM;
 }
 else
   session->variables.time_zone= global_system_variables.time_zone;
 pthread_mutex_unlock(&LOCK_global_system_variables);
}


bool sys_var_session_lc_time_names::check(Session *, set_var *var)
{
  MY_LOCALE *locale_match;

  if (var->value->result_type() == INT_RESULT)
  {
    if (!(locale_match= my_locale_by_number((uint32_t) var->value->val_int())))
    {
      char buf[20];
      int10_to_str((int) var->value->val_int(), buf, -10);
      my_printf_error(ER_UNKNOWN_ERROR, "Unknown locale: '%s'", MYF(0), buf);
      return 1;
    }
  }
  else // STRING_RESULT
  {
    char buff[6];
    String str(buff, sizeof(buff), &my_charset_utf8_general_ci), *res;
    if (!(res=var->value->val_str(&str)))
    {
      my_error(ER_WRONG_VALUE_FOR_VAR, MYF(0), name.c_str(), "NULL");
      return 1;
    }
    const char *locale_str= res->c_ptr();
    if (!(locale_match= my_locale_by_name(locale_str)))
    {
      my_printf_error(ER_UNKNOWN_ERROR,
                      "Unknown locale: '%s'", MYF(0), locale_str);
      return 1;
    }
  }

  var->save_result.locale_value= locale_match;
  return 0;
}


bool sys_var_session_lc_time_names::update(Session *session, set_var *var)
{
  if (var->type == OPT_GLOBAL)
    global_system_variables.lc_time_names= var->save_result.locale_value;
  else
    session->variables.lc_time_names= var->save_result.locale_value;
  return 0;
}


unsigned char *sys_var_session_lc_time_names::value_ptr(Session *session,
                                                        enum_var_type type,
                                                        const LEX_STRING *)
{
  return type == OPT_GLOBAL ?
                 (unsigned char *) global_system_variables.lc_time_names->name :
                 (unsigned char *) session->variables.lc_time_names->name;
}


void sys_var_session_lc_time_names::set_default(Session *session, enum_var_type type)
{
  if (type == OPT_GLOBAL)
    global_system_variables.lc_time_names= my_default_lc_time_names;
  else
    session->variables.lc_time_names= global_system_variables.lc_time_names;
}

/*
  Handling of microseoncds given as seconds.part_seconds

  NOTES
    The argument to long query time is in seconds in decimal
    which is converted to uint64_t integer holding microseconds for storage.
    This is used for handling long_query_time
*/

bool sys_var_microseconds::update(Session *session, set_var *var)
{
  double num= var->value->val_real();
  int64_t microseconds;
  if (num > (double) option_limits->max_value)
    num= (double) option_limits->max_value;
  if (num < (double) option_limits->min_value)
    num= (double) option_limits->min_value;
  microseconds= (int64_t) (num * 1000000.0 + 0.5);
  if (var->type == OPT_GLOBAL)
  {
    pthread_mutex_lock(&LOCK_global_system_variables);
    (global_system_variables.*offset)= microseconds;
    pthread_mutex_unlock(&LOCK_global_system_variables);
  }
  else
    session->variables.*offset= microseconds;
  return 0;
}


void sys_var_microseconds::set_default(Session *session, enum_var_type type)
{
  int64_t microseconds= (int64_t) (option_limits->def_value * 1000000.0);
  if (type == OPT_GLOBAL)
  {
    pthread_mutex_lock(&LOCK_global_system_variables);
    global_system_variables.*offset= microseconds;
    pthread_mutex_unlock(&LOCK_global_system_variables);
  }
  else
    session->variables.*offset= microseconds;
}

/*
  Functions to update session->options bits
*/

static bool set_option_bit(Session *session, set_var *var)
{
  sys_var_session_bit *sys_var= ((sys_var_session_bit*) var->var);
  if ((var->save_result.uint32_t_value != 0) == sys_var->reverse)
    session->options&= ~sys_var->bit_flag;
  else
    session->options|= sys_var->bit_flag;
  return 0;
}


static bool set_option_autocommit(Session *session, set_var *var)
{
  /* The test is negative as the flag we use is NOT autocommit */

  uint64_t org_options= session->options;

  if (var->save_result.uint32_t_value != 0)
    session->options&= ~((sys_var_session_bit*) var->var)->bit_flag;
  else
    session->options|= ((sys_var_session_bit*) var->var)->bit_flag;

  if ((org_options ^ session->options) & OPTION_NOT_AUTOCOMMIT)
  {
    if ((org_options & OPTION_NOT_AUTOCOMMIT))
    {
      /* We changed to auto_commit mode */
      session->options&= ~(uint64_t) (OPTION_BEGIN);
      session->transaction.all.modified_non_trans_table= false;
      session->server_status|= SERVER_STATUS_AUTOCOMMIT;
      if (ha_commit(session))
	return 1;
    }
    else
    {
      session->transaction.all.modified_non_trans_table= false;
      session->server_status&= ~SERVER_STATUS_AUTOCOMMIT;
    }
  }
  return 0;
}

static int check_pseudo_thread_id(Session *, set_var *var)
{
  var->save_result.uint64_t_value= var->value->val_int();
  return 0;
}

static unsigned char *get_warning_count(Session *session)
{
  session->sys_var_tmp.uint32_t_value=
    (session->warn_count[(uint32_t) DRIZZLE_ERROR::WARN_LEVEL_NOTE] +
     session->warn_count[(uint32_t) DRIZZLE_ERROR::WARN_LEVEL_ERROR] +
     session->warn_count[(uint32_t) DRIZZLE_ERROR::WARN_LEVEL_WARN]);
  return (unsigned char*) &session->sys_var_tmp.uint32_t_value;
}

static unsigned char *get_error_count(Session *session)
{
  session->sys_var_tmp.uint32_t_value=
    session->warn_count[(uint32_t) DRIZZLE_ERROR::WARN_LEVEL_ERROR];
  return (unsigned char*) &session->sys_var_tmp.uint32_t_value;
}


/**
  Get the tmpdir that was specified or chosen by default.

  This is necessary because if the user does not specify a temporary
  directory via the command line, one is chosen based on the environment
  or system defaults.  But we can't just always use drizzle_tmpdir, because
  that is actually a call to my_tmpdir() which cycles among possible
  temporary directories.

  @param session		thread handle

  @retval
    ptr		pointer to NUL-terminated string
*/
static unsigned char *get_tmpdir(Session *)
{
  assert(drizzle_tmpdir);
  return (unsigned char*)drizzle_tmpdir;
}

/****************************************************************************
  Main handling of variables:
  - Initialisation
  - Searching during parsing
  - Update loop
****************************************************************************/

/**
  Find variable name in option my_getopt structure used for
  command line args.

  @param opt	option structure array to search in
  @param name	variable name

  @retval
    0		Error
  @retval
    ptr		pointer to option structure
*/

static struct my_option *find_option(struct my_option *opt, const char *name)
{
  uint32_t length=strlen(name);
  for (; opt->name; opt++)
  {
    if (!getopt_compare_strings(opt->name, name, length) &&
	!opt->name[length])
    {
      /*
	Only accept the option if one can set values through it.
	If not, there is no default value or limits in the option.
      */
      return (opt->value) ? opt : 0;
    }
  }
  return 0;
}


/*
  Add variables to the dynamic hash of system variables

  SYNOPSIS
    mysql_add_sys_var_chain()
    first       Pointer to first system variable to add
    long_opt    (optional)command line arguments may be tied for limit checks.

  RETURN VALUES
    0           SUCCESS
    otherwise   FAILURE
*/


int mysql_add_sys_var_chain(sys_var *first, struct my_option *long_options)
{
  sys_var *var;
  /* A write lock should be held on LOCK_system_variables_hash */

  for (var= first; var; var= var->getNext())
  {

    /* this fails if there is a conflicting variable name. */
    if (system_variable_hash.add(var))
    {
      return 1;
    } 
    if (long_options)
      var->setOptionLimits(find_option(long_options, var->getName().c_str()));
  }
  return 0;

}


/*
  Remove variables to the dynamic hash of system variables

  SYNOPSIS
    mysql_del_sys_var_chain()
    first       Pointer to first system variable to remove

  RETURN VALUES
    0           SUCCESS
    otherwise   FAILURE
*/

int mysql_del_sys_var_chain(sys_var *first)
{
  int result= 0;

  /* A write lock should be held on LOCK_system_variables_hash */
  for (sys_var *var= first; var; var= var->getNext())
  {
    system_variable_hash.remove(var);
  }
  return result;
}



/*
  Constructs an array of system variables for display to the user.

  SYNOPSIS
    enumerate_sys_vars()
    session         current thread
    sorted      If TRUE, the system variables should be sorted

  RETURN VALUES
    pointer     Array of SHOW_VAR elements for display
    NULL        FAILURE
*/

SHOW_VAR* enumerate_sys_vars(Session *session, bool)
{
  int fixed_count= fixed_show_vars.elements;
  int size= sizeof(SHOW_VAR) * (system_variable_hash.size() + fixed_count + 1);
  SHOW_VAR *result= (SHOW_VAR*) session->alloc(size);

  if (result)
  {
    SHOW_VAR *show= result + fixed_count;
    memcpy(result, fixed_show_vars.buffer, fixed_count * sizeof(SHOW_VAR));

    drizzled::Registry<sys_var *>::const_iterator iter;
    for(iter= system_variable_hash.begin();
        iter != system_variable_hash.end();
        iter++)
    {
      sys_var *var= *iter;
      show->name= var->getName().c_str();
      show->value= (char*) var;
      show->type= SHOW_SYS;
      show++;
    }

    /* make last element empty */
    memset(show, 0, sizeof(SHOW_VAR));
  }
  return result;
}


/*
  Initialize the system variables

  SYNOPSIS
    set_var_init()

  RETURN VALUES
    0           SUCCESS
    otherwise   FAILURE
*/

int set_var_init()
{
  uint32_t count= 0;

  for (sys_var *var= vars.first; var; var= var->getNext(), count++) {};

  if (my_init_dynamic_array(&fixed_show_vars, sizeof(SHOW_VAR),
                            FIXED_VARS_SIZE + 64, 64))
    goto error;

  fixed_show_vars.elements= FIXED_VARS_SIZE;
  memcpy(fixed_show_vars.buffer, fixed_vars, sizeof(fixed_vars));

  vars.last->setNext(NULL);
  if (mysql_add_sys_var_chain(vars.first, my_long_options))
    goto error;

  return(0);

error:
  fprintf(stderr, "failed to initialize system variables");
  return(1);
}


void set_var_free()
{
  delete_dynamic(&fixed_show_vars);
}


/*
  Add elements to the dynamic list of read-only system variables.

  SYNOPSIS
    mysql_append_static_vars()
    show_vars	Pointer to start of array
    count       Number of elements

  RETURN VALUES
    0           SUCCESS
    otherwise   FAILURE
*/
int mysql_append_static_vars(const SHOW_VAR *show_vars, uint32_t count)
{
  for (; count > 0; count--, show_vars++)
    if (insert_dynamic(&fixed_show_vars, (unsigned char*) show_vars))
      return 1;
  return 0;
}


/**
  Find a user set-table variable.

  @param str	   Name of system variable to find
  @param length    Length of variable.  zero means that we should use strlen()
                   on the variable
  @param no_error  Refuse to emit an error, even if one occurred.

  @retval
    pointer	pointer to variable definitions
  @retval
    0		Unknown variable (error message is given)
*/

sys_var *intern_find_sys_var(const char *str, uint32_t, bool no_error)
{
  /*
    This function is only called from the sql_plugin.cc.
    A lock on LOCK_system_variable_hash should be held
  */
  sys_var *result= system_variable_hash.find(str);
  if (result == NULL)
  {
    if (no_error)
    {
      return NULL;
    }
    else
    {
      my_error(ER_UNKNOWN_SYSTEM_VARIABLE, MYF(0), (char*) str);
      return NULL;
    }
  }

  return result;
}


/**
  Execute update of all variables.

  First run a check of all variables that all updates will go ok.
  If yes, then execute all updates, returning an error if any one failed.

  This should ensure that in all normal cases none all or variables are
  updated.

  @param Session		Thread id
  @param var_list       List of variables to update

  @retval
    0	ok
  @retval
    1	ERROR, message sent (normally no variables was updated)
  @retval
    -1  ERROR, message not sent
*/

int sql_set_variables(Session *session, List<set_var_base> *var_list)
{
  int error;
  List_iterator_fast<set_var_base> it(*var_list);

  set_var_base *var;
  while ((var=it++))
  {
    if ((error= var->check(session)))
      goto err;
  }
  if (!(error= test(session->is_error())))
  {
    it.rewind();
    while ((var= it++))
      error|= var->update(session);         // Returns 0, -1 or 1
  }

err:
  free_underlaid_joins(session, &session->lex->select_lex);
  return(error);
}


/*****************************************************************************
  Functions to handle SET mysql_internal_variable=const_expr
*****************************************************************************/

int set_var::check(Session *session)
{
  if (var->is_readonly())
  {
    my_error(ER_INCORRECT_GLOBAL_LOCAL_VAR, MYF(0), var->getName().c_str(), "read only");
    return -1;
  }
  if (var->check_type(type))
  {
    int err= type == OPT_GLOBAL ? ER_LOCAL_VARIABLE : ER_GLOBAL_VARIABLE;
    my_error(err, MYF(0), var->getName().c_str());
    return -1;
  }
  /* value is a NULL pointer if we are using SET ... = DEFAULT */
  if (!value)
  {
    if (var->check_default(type))
    {
      my_error(ER_NO_DEFAULT, MYF(0), var->getName().c_str());
      return -1;
    }
    return 0;
  }

  if ((!value->fixed &&
       value->fix_fields(session, &value)) || value->check_cols(1))
    return -1;
  if (var->check_update_type(value->result_type()))
  {
    my_error(ER_WRONG_TYPE_FOR_VAR, MYF(0), var->getName().c_str());
    return -1;
  }
  return var->check(session, this) ? -1 : 0;
}

/**
  Update variable

  @param   session    thread handler
  @returns 0|1    ok or	ERROR

  @note ERROR can be only due to abnormal operations involving
  the server's execution evironment such as
  out of memory, hard disk failure or the computer blows up.
  Consider set_var::check() method if there is a need to return
  an error due to logics.
*/
int set_var::update(Session *session)
{
  if (! value)
    var->set_default(session, type);
  else if (var->update(session, this))
    return -1;				// should never happen
  if (var->getAfterUpdateTrigger())
    (*var->getAfterUpdateTrigger())(session, type);
  return 0;
}

/*****************************************************************************
  Functions to handle SET @user_variable=const_expr
*****************************************************************************/

int set_var_user::check(Session *session)
{
  /*
    Item_func_set_user_var can't substitute something else on its place =>
    0 can be passed as last argument (reference on item)
  */
  return (user_var_item->fix_fields(session, (Item**) 0) ||
	  user_var_item->check(0)) ? -1 : 0;
}


int set_var_user::update(Session *)
{
  if (user_var_item->update())
  {
    /* Give an error if it's not given already */
    my_message(ER_SET_CONSTANTS_ONLY, ER(ER_SET_CONSTANTS_ONLY), MYF(0));
    return -1;
  }
  return 0;
}

/****************************************************************************
 Functions to handle table_type
****************************************************************************/

/* Based upon sys_var::check_enum() */

bool sys_var_session_storage_engine::check(Session *session, set_var *var)
{
  char buff[STRING_BUFFER_USUAL_SIZE];
  const char *value;
  String str(buff, sizeof(buff), &my_charset_utf8_general_ci), *res;

  var->save_result.storage_engine= NULL;
  if (var->value->result_type() == STRING_RESULT)
  {
    res= var->value->val_str(&str);
    if (res == NULL || res->ptr() == NULL)
    {
      value= "NULL";
      goto err;
    }
    else
    {
      const std::string engine_name(res->ptr());
      plugin::StorageEngine *engine;
      var->save_result.storage_engine= plugin::StorageEngine::findByName(session, engine_name);
      if (var->save_result.storage_engine == NULL)
      {
        value= res->c_ptr();
        goto err;
      }
      engine= var->save_result.storage_engine;
    }
    return 0;
  }
  value= "unknown";

err:
  my_error(ER_UNKNOWN_STORAGE_ENGINE, MYF(0), value);
  return 1;
}


unsigned char *sys_var_session_storage_engine::value_ptr(Session *session,
                                                         enum_var_type type,
                                                         const LEX_STRING *)
{
  unsigned char* result;
  string engine_name;
  plugin::StorageEngine *engine= session->variables.*offset;
  if (type == OPT_GLOBAL)
    engine= global_system_variables.*offset;
  engine_name= engine->getName();
  result= (unsigned char *) session->strmake(engine_name.c_str(),
                                             engine_name.size());
  return result;
}


void sys_var_session_storage_engine::set_default(Session *session, enum_var_type type)
{
  plugin::StorageEngine *old_value, *new_value, **value;
  if (type == OPT_GLOBAL)
  {
    value= &(global_system_variables.*offset);
    new_value= myisam_engine;
  }
  else
  {
    value= &(session->variables.*offset);
    new_value= global_system_variables.*offset;
  }
  assert(new_value);
  old_value= *value;
  *value= new_value;
}


bool sys_var_session_storage_engine::update(Session *session, set_var *var)
{
  plugin::StorageEngine **value= &(global_system_variables.*offset), *old_value;
   if (var->type != OPT_GLOBAL)
     value= &(session->variables.*offset);
  old_value= *value;
  if (old_value != var->save_result.storage_engine)
  {
    *value= var->save_result.storage_engine;
  }
  return 0;
}

bool
sys_var_session_optimizer_switch::
symbolic_mode_representation(Session *session, uint32_t val, LEX_STRING *rep)
{
  char buff[STRING_BUFFER_USUAL_SIZE*8];
  String tmp(buff, sizeof(buff), &my_charset_utf8_general_ci);

  tmp.length(0);

  for (uint32_t i= 0; val; val>>= 1, i++)
  {
    if (val & 1)
    {
      tmp.append(optimizer_switch_typelib.type_names[i],
                 optimizer_switch_typelib.type_lengths[i]);
      tmp.append(',');
    }
  }

  if (tmp.length())
    tmp.length(tmp.length() - 1); /* trim the trailing comma */

  rep->str= session->strmake(tmp.ptr(), tmp.length());

  rep->length= rep->str ? tmp.length() : 0;

  return rep->length != tmp.length();
}


unsigned char *sys_var_session_optimizer_switch::value_ptr(Session *session,
                                                           enum_var_type type,
                                                           const LEX_STRING *)
{
  LEX_STRING opts;
  uint32_t val= ((type == OPT_GLOBAL) ? global_system_variables.*offset :
                  session->variables.*offset);
  (void) symbolic_mode_representation(session, val, &opts);
  return (unsigned char *) opts.str;
}


void sys_var_session_optimizer_switch::set_default(Session *session, enum_var_type type)
{
  if (type == OPT_GLOBAL)
    global_system_variables.*offset= 0;
  else
    session->variables.*offset= global_system_variables.*offset;
}


/****************************************************************************
  Used templates
****************************************************************************/

#ifdef HAVE_EXPLICIT_TEMPLATE_INSTANTIATION
template class List<set_var_base>;
template class List_iterator_fast<set_var_base>;
template class I_List_iterator<NAMED_LIST>;
#endif
