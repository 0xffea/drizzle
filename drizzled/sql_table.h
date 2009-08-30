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
  @file

  Routines to drop, repair, optimize, analyze, and check a schema table

*/
#ifndef DRIZZLED_SQL_TABLE_H
#define DRIZZLED_SQL_TABLE_H

class Session;
class TableList;
typedef struct st_ha_check_opt HA_CHECK_OPT;
class Table;

namespace drizzled { namespace message { class Table; } }

bool mysql_rm_table(Session *session,TableList *tables, bool if_exists,
                    bool drop_temporary);
int mysql_rm_table_part2(Session *session, TableList *tables, bool if_exists,
                         bool drop_temporary, bool log_query);
bool quick_rm_table(StorageEngine *, const char *db,
                    const char *table_name, bool is_tmp);
void close_cached_table(Session *session, Table *table);

void wait_while_table_is_used(Session *session, Table *table,
                              enum ha_extra_function function);

bool mysql_alter_table(Session *session, char *new_db, char *new_name,
                       HA_CREATE_INFO *create_info,
                       drizzled::message::Table *create_proto,
                       TableList *table_list,
                       Alter_info *alter_info,
                       uint32_t order_num, order_st *order, bool ignore);
bool mysql_checksum_table(Session* session, TableList* table_list,
                          HA_CHECK_OPT* check_opt);
bool mysql_check_table(Session* session, TableList* table_list,
                       HA_CHECK_OPT* check_opt);
bool mysql_analyze_table(Session* session, TableList* table_list,
                         HA_CHECK_OPT* check_opt);
bool mysql_optimize_table(Session* session, TableList* table_list,
                          HA_CHECK_OPT* check_opt);

void write_bin_log(Session *session, bool clear_error,
                   char const *query, size_t query_length);

bool is_primary_key(KEY *key_info);
const char* is_primary_key_name(const char* key_name);

#endif /* DRIZZLED_SQL_TABLE_H */
