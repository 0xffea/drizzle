/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
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

#include <drizzled/server_includes.h>
#include <drizzled/show.h>
#include <drizzled/lock.h>
#include <drizzled/session.h>
#include <drizzled/statement/rename_table.h>

namespace drizzled
{

bool statement::RenameTable::execute()
{
  TableList *first_table= (TableList *) session->lex->select_lex.table_list.first;
  TableList *all_tables= session->lex->query_tables;
  assert(first_table == all_tables && first_table != 0);
  TableList *table;
  for (table= first_table; table; table= table->next_local->next_local)
  {
    TableList old_list, new_list;
    /*
       we do not need initialize old_list and new_list because we will
       come table[0] and table->next[0] there
     */
    old_list= table[0];
    new_list= table->next_local[0];
  }

  if (! session->endActiveTransaction() || renameTables(first_table))
  {
    return true;
  }
  return false;
}

bool statement::RenameTable::renameTables(TableList *table_list)
{
  bool error= true;
  TableList *ren_table= NULL;

  /*
    Avoid problems with a rename on a table that we have locked or
    if the user is trying to to do this in a transcation context
  */
  if (session->inTransaction())
  {
    my_message(ER_LOCK_OR_ACTIVE_TRANSACTION, ER(ER_LOCK_OR_ACTIVE_TRANSACTION), MYF(0));
    return true;
  }

  if (wait_if_global_read_lock(session, 0, 1))
    return true;

  pthread_mutex_lock(&LOCK_open); /* Rename table lock for exclusive access */
  if (lock_table_names_exclusively(session, table_list))
  {
    pthread_mutex_unlock(&LOCK_open);
    goto err;
  }

  error= false;
  ren_table= renameTablesInList(table_list, 0);
  if (ren_table)
  {
    /* Rename didn't succeed;  rename back the tables in reverse order */
    TableList *table;

    /* Reverse the table list */
    table_list= reverseTableList(table_list);

    /* Find the last renamed table */
    for (table= table_list;
         table->next_local != ren_table;
         table= table->next_local->next_local) 
    { /* do nothing */ }
    table= table->next_local->next_local;		// Skip error table
    /* Revert to old names */
    renameTablesInList(table, 1);
    error= true;
  }
  /*
    An exclusive lock on table names is satisfactory to ensure
    no other thread accesses this table.
    We still should unlock LOCK_open as early as possible, to provide
    higher concurrency - query_cache_invalidate can take minutes to
    complete.
  */
  pthread_mutex_unlock(&LOCK_open);

  /* Lets hope this doesn't fail as the result will be messy */
  if (! error)
  {
    write_bin_log(session, true, session->query, session->query_length);
    session->my_ok();
  }

  pthread_mutex_lock(&LOCK_open); /* unlock all tables held */
  unlock_table_names(table_list, NULL);
  pthread_mutex_unlock(&LOCK_open);

err:
  start_waiting_global_read_lock(session);

  return error;
}

TableList *statement::RenameTable::reverseTableList(TableList *table_list)
{
  TableList *prev= NULL;

  while (table_list)
  {
    TableList *next= table_list->next_local;
    table_list->next_local= prev;
    prev= table_list;
    table_list= next;
  }
  return prev;
}

bool statement::RenameTable::rename(TableList *ren_table,
                                    const char *new_db,
                                    const char *new_table_name,
                                    bool skip_error)
{
  bool rc= true;
  const char *new_alias, *old_alias;

  {
    old_alias= ren_table->table_name;
    new_alias= new_table_name;
  }

  plugin::StorageEngine *engine= NULL;
  message::Table table_proto;
  char path[FN_REFLEN];
  size_t length;

  plugin::Registry &plugins= plugin::Registry::singleton();
  length= build_table_filename(path, sizeof(path),
                               ren_table->db, old_alias, false);

  if (plugins.storage_engine.getTableProto(path, &table_proto) != EEXIST)
  {
    my_error(ER_NO_SUCH_TABLE, MYF(0), ren_table->db, old_alias);
    return true;
  }

  engine= ha_resolve_by_name(session, table_proto.engine().name());

  length= build_table_filename(path, sizeof(path),
                               new_db, new_alias, false);

  if (plugins.storage_engine.getTableProto(path, NULL) != ENOENT)
  {
    my_error(ER_TABLE_EXISTS_ERROR, MYF(0), new_alias);
    return 1; // This can't be skipped
  }

  rc= mysql_rename_table(engine,
                         ren_table->db, old_alias,
                         new_db, new_alias, 0);
  if (rc && ! skip_error)
    return true;

  return false;
}

TableList *statement::RenameTable::renameTablesInList(TableList *table_list,
                                                      bool skip_error)
{
  TableList *ren_table, *new_table;

  for (ren_table= table_list; ren_table; ren_table= new_table->next_local)
  {
    new_table= ren_table->next_local;
    if (rename(ren_table, new_table->db, new_table->table_name, skip_error))
      return ren_table;
  }
  return 0;
} 

} /* namespace drizzled */
