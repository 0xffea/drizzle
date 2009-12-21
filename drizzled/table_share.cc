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

/*
  This class is shared between different table objects. There is one
  instance of table share per one table in the database.
*/

/* Basic functions needed by many modules */
#include "drizzled/server_includes.h"

#include <assert.h>

#include "drizzled/error.h"
#include "drizzled/gettext.h"
#include "drizzled/sql_base.h"
#include "drizzled/hash.h"

using namespace std;

extern size_t table_def_size;
typedef drizzled::hash_map<string, TableShare *> TableDefCache;
TableDefCache table_def_cache;
static pthread_mutex_t LOCK_table_share;
bool table_def_inited= false;

/*****************************************************************************
  Functions to handle table definition cach (TableShare)
 *****************************************************************************/


void TableShare::cacheStart(void)
{
  pthread_mutex_init(&LOCK_table_share, MY_MUTEX_INIT_FAST);
  table_def_inited= true;
  /* 
   * This is going to overalloc a bit - as rehash sets the number of
   * buckets, not the number of elements. BUT, it'll allow us to not need
   * to rehash later on as the hash_map grows.
   */
  table_def_cache.rehash(table_def_size);
}


void TableShare::cacheStop(void)
{
  if (table_def_inited)
  {
    table_def_inited= false;
    pthread_mutex_destroy(&LOCK_table_share);
  }
}


/**
 * @TODO: This should return size_t
 */
uint32_t cached_table_definitions(void)
{
  return static_cast<uint32_t>(table_def_cache.size());
}


/*
  Mark that we are not using table share anymore.

  SYNOPSIS
  release()
  share		Table share

  IMPLEMENTATION
  If ref_count goes to zero and (we have done a refresh or if we have
  already too many open table shares) then delete the definition.
*/

void TableShare::release(TableShare *share)
{
  bool to_be_deleted= false;
  safe_mutex_assert_owner(&LOCK_open);

  pthread_mutex_lock(&share->mutex);
  if (!--share->ref_count)
    to_be_deleted= true;

  if (to_be_deleted)
  {
    const string key_string(share->table_cache_key.str,
                            share->table_cache_key.length);
    TableDefCache::iterator iter= table_def_cache.find(key_string);
    if (iter != table_def_cache.end())
    {
      (*iter).second->free_table_share();
      table_def_cache.erase(iter);
    }
    return;
  }
  pthread_mutex_unlock(&share->mutex);
}

void TableShare::release(const char *key, uint32_t key_length)
{
  const string key_string(key, key_length);

  TableDefCache::iterator iter= table_def_cache.find(key_string);
  if (iter != table_def_cache.end())
  {
    TableShare *share= (*iter).second;
    share->version= 0;                          // Mark for delete
    if (share->ref_count == 0)
    {
      pthread_mutex_lock(&share->mutex);
      share->free_table_share();
      table_def_cache.erase(key_string);
    }
  }
}


static TableShare *foundTableShare(TableShare *share)
{
  /*
    We found an existing table definition. Return it if we didn't get
    an error when reading the table definition from file.
  */

  /* We must do a lock to ensure that the structure is initialized */
  (void) pthread_mutex_lock(&share->mutex);
  if (share->error)
  {
    /* Table definition contained an error */
    share->open_table_error(share->error, share->open_errno, share->errarg);
    (void) pthread_mutex_unlock(&share->mutex);

    return NULL;
  }

  share->ref_count++;
  (void) pthread_mutex_unlock(&share->mutex);

  return share;
}

/*
  Get TableShare for a table.

  get_table_share()
  session			Thread handle
  table_list		Table that should be opened
  key			Table cache key
  key_length		Length of key
  error			out: Error code from open_table_def()

  IMPLEMENTATION
  Get a table definition from the table definition cache.
  If it doesn't exist, create a new from the table definition file.

  NOTES
  We must have wrlock on LOCK_open when we come here
  (To be changed later)

  RETURN
  0  Error
#  Share for table
*/

TableShare *TableShare::getShare(Session *session, 
                                 TableList *table_list, char *key,
                                 uint32_t key_length, uint32_t, int *error)
{
  const string key_string(key, key_length);
  TableShare *share= NULL;

  *error= 0;

  /* Read table definition from cache */
  TableDefCache::iterator iter= table_def_cache.find(key_string);
  if (iter != table_def_cache.end())
  {
    share= (*iter).second;
    return foundTableShare(share);
  }

  if (!(share= alloc_table_share(table_list, key, key_length)))
  {
    return NULL;
  }

  /*
    Lock mutex to be able to read table definition from file without
    conflicts
  */
  (void) pthread_mutex_lock(&share->mutex);

  /**
   * @TODO: we need to eject something if we exceed table_def_size
   */
  pair<TableDefCache::iterator, bool> ret=
    table_def_cache.insert(make_pair(key_string, share));
  if (ret.second == false)
  {
    share->free_table_share();
    return NULL;
  }
  
  if (open_table_def(*session, share))
  {
    *error= share->error;
    table_def_cache.erase(key_string);
    share->free_table_share();
    return NULL;
  }
  share->ref_count++;				// Mark in use
  (void) pthread_mutex_unlock(&share->mutex);
  return share;

}


/*
  Check if table definition exits in cache

  SYNOPSIS
  get_cached_table_share()
  db			Database name
  table_name		Table name

  RETURN
  0  Not cached
#  TableShare for table
*/

TableShare *TableShare::getShare(const char *db, const char *table_name)
{
  char key[NAME_LEN*2+2];
  uint32_t key_length;
  safe_mutex_assert_owner(&LOCK_open);

  key_length= TableShare::createKey(key, db, table_name);

  const string key_string(key, key_length);
  TableDefCache::iterator iter= table_def_cache.find(key_string);
  if (iter != table_def_cache.end())
  {
    return (*iter).second;
  }
  else
  {
    return NULL;
  }
}
