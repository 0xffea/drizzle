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

#include "drizzled/base.h"

namespace drizzled
{

class Session;
class TableList;
typedef struct st_ha_check_opt HA_CHECK_OPT;
class Table;
typedef struct st_key KEY;
typedef struct st_ha_create_information HA_CREATE_INFO;
class AlterInfo;
class Cursor;

/* Flags for conversion functions. */
static const uint32_t FN_FROM_IS_TMP(1 << 0);
static const uint32_t FN_TO_IS_TMP(1 << 0);

namespace message { class Table; }
class TableIdentifier;

int mysql_rm_table_part2(Session *session, TableList *tables, bool if_exists,
                         bool drop_temporary);
void write_bin_log_drop_table(Session *session,
                              bool if_exists, const char *db_name,
                              const char *table_name);
bool quick_rm_table(Session& session,
                    TableIdentifier &identifier);
void close_cached_table(Session *session, Table *table);

void wait_while_table_is_used(Session *session, Table *table,
                              enum ha_extra_function function);

bool mysql_check_table(Session* session, TableList* table_list,
                       HA_CHECK_OPT* check_opt);
bool mysql_analyze_table(Session* session, TableList* table_list,
                         HA_CHECK_OPT* check_opt);
bool mysql_optimize_table(Session* session, TableList* table_list,
                          HA_CHECK_OPT* check_opt);

void write_bin_log(Session *session,
                   char const *query);

bool is_primary_key(KEY *key_info);
const char* is_primary_key_name(const char* key_name);
bool check_engine(Session *, const char *, message::Table *, HA_CREATE_INFO *);
void set_table_default_charset(HA_CREATE_INFO *create_info, const char *db);
int mysql_prepare_create_table(Session *session,
                               HA_CREATE_INFO *create_info,
                               message::Table &create_proto,
                               AlterInfo *alter_info,
                               bool tmp_table,
                               uint32_t *db_options,
                               KEY **key_info_buffer,
                               uint32_t *key_count,
                               int select_field_count);
} /* namespace drizzled */

#endif /* DRIZZLED_SQL_TABLE_H */
