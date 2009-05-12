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

#ifndef DRIZZLED_LOCK_H
#define DRIZZLED_LOCK_H

#include <mysys/definitions.h>

class Session;
class Table;
class TableList;
typedef struct drizzled_lock_st DRIZZLE_LOCK;

DRIZZLE_LOCK *mysql_lock_tables(Session *session, Table **table, uint32_t count,
                                uint32_t flags, bool *need_reopen);
/* mysql_lock_tables() and open_table() flags bits */
#define DRIZZLE_LOCK_IGNORE_GLOBAL_READ_LOCK      0x0001
#define DRIZZLE_LOCK_IGNORE_FLUSH                 0x0002
#define DRIZZLE_LOCK_NOTIFY_IF_NEED_REOPEN        0x0004
#define DRIZZLE_OPEN_TEMPORARY_ONLY               0x0008

void mysql_unlock_tables(Session *session, DRIZZLE_LOCK *sql_lock);
void mysql_unlock_read_tables(Session *session, DRIZZLE_LOCK *sql_lock);
void mysql_unlock_some_tables(Session *session, Table **table,uint32_t count);
void mysql_lock_remove(Session *session, DRIZZLE_LOCK *locked,Table *table,
                       bool always_unlock);
void mysql_lock_abort(Session *session, Table *table, bool upgrade_lock);
bool mysql_lock_abort_for_thread(Session *session, Table *table);
DRIZZLE_LOCK *mysql_lock_merge(DRIZZLE_LOCK *a,DRIZZLE_LOCK *b);
TableList *mysql_lock_have_duplicate(Session *session, TableList *needle,
                                      TableList *haystack);
bool lock_global_read_lock(Session *session);
void unlock_global_read_lock(Session *session);
bool wait_if_global_read_lock(Session *session, bool abort_on_refresh,
                              bool is_not_commit);
void start_waiting_global_read_lock(Session *session);
bool make_global_read_lock_block_commit(Session *session);
bool set_protect_against_global_read_lock(void);
void unset_protect_against_global_read_lock(void);
void broadcast_refresh(void);
int try_transactional_lock(Session *session, TableList *table_list);
int check_transactional_lock(Session *session, TableList *table_list);
int set_handler_table_locks(Session *session, TableList *table_list,
                            bool transactional);

/* Lock based on name */
int lock_and_wait_for_table_name(Session *session, TableList *table_list);
int lock_table_name(Session *session, TableList *table_list, bool check_in_use);
void unlock_table_name(Session *session, TableList *table_list);
bool wait_for_locked_table_names(Session *session, TableList *table_list);
bool lock_table_names(Session *session, TableList *table_list);
void unlock_table_names(Session *session, TableList *table_list,
			TableList *last_table);
bool lock_table_names_exclusively(Session *session, TableList *table_list);
bool is_table_name_exclusively_locked_by_this_thread(Session *session,
                                                     TableList *table_list);
bool is_table_name_exclusively_locked_by_this_thread(Session *session,
                                                     unsigned char *key,
                                                     int key_length);

#endif /* DRIZZLED_LOCK_H */
