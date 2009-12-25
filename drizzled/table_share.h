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

#ifndef DRIZZLED_TABLE_SHARE_H
#define DRIZZLED_TABLE_SHARE_H

#include <string>

#include "drizzled/typelib.h"
#include "drizzled/my_hash.h"
#include "drizzled/memory/root.h"
#include "drizzled/message/table.pb.h"

class TableShare
{
public:
  TableShare() :
    table_category(TABLE_UNKNOWN_CATEGORY),
    open_count(0),
    field(NULL),
    found_next_number_field(NULL),
    timestamp_field(NULL),
    key_info(NULL),
    blob_field(NULL),
    intervals(NULL),
    default_values(NULL),
    block_size(0),
    version(0),
    timestamp_offset(0),
    reclength(0),
    stored_rec_length(0),
    row_type(ROW_TYPE_DEFAULT),
    max_rows(0),
    table_proto(NULL),
    storage_engine(NULL),
    tmp_table(NO_TMP_TABLE),
    ref_count(0),
    null_bytes(0),
    last_null_bit_pos(0),
    fields(0),
    rec_buff_length(0),
    keys(0),
    key_parts(0),
    max_key_length(0),
    max_unique_length(0),
    total_key_length(0),
    uniques(0),
    null_fields(0),
    blob_fields(0),
    timestamp_field_offset(0),
    varchar_fields(0),
    db_create_options(0),
    db_options_in_use(0),
    db_record_offset(0),
    rowid_field_offset(0),
    primary_key(0),
    next_number_index(0),
    next_number_key_offset(0),
    next_number_keypart(0),
    error(0),
    open_errno(0),
    errarg(0),
    column_bitmap_size(0),
    blob_ptr_size(0),
    db_low_byte_first(false),
    name_lock(false),
    replace_with_name_lock(false),
    waiting_on_cond(false),
    keys_in_use(0),
    keys_for_keyread(0)
  {
    init();
  }

  TableShare(const char *key,
             uint32_t key_length,
             const char *new_table_name,
             const char *new_path) :
    table_category(TABLE_UNKNOWN_CATEGORY),
    open_count(0),
    field(NULL),
    found_next_number_field(NULL),
    timestamp_field(NULL),
    key_info(NULL),
    blob_field(NULL),
    intervals(NULL),
    default_values(NULL),
    block_size(0),
    version(0),
    timestamp_offset(0),
    reclength(0),
    stored_rec_length(0),
    row_type(ROW_TYPE_DEFAULT),
    max_rows(0),
    table_proto(NULL),
    storage_engine(NULL),
    tmp_table(NO_TMP_TABLE),
    ref_count(0),
    null_bytes(0),
    last_null_bit_pos(0),
    fields(0),
    rec_buff_length(0),
    keys(0),
    key_parts(0),
    max_key_length(0),
    max_unique_length(0),
    total_key_length(0),
    uniques(0),
    null_fields(0),
    blob_fields(0),
    timestamp_field_offset(0),
    varchar_fields(0),
    db_create_options(0),
    db_options_in_use(0),
    db_record_offset(0),
    rowid_field_offset(0),
    primary_key(0),
    next_number_index(0),
    next_number_key_offset(0),
    next_number_keypart(0),
    error(0),
    open_errno(0),
    errarg(0),
    column_bitmap_size(0),
    blob_ptr_size(0),
    db_low_byte_first(false),
    name_lock(false),
    replace_with_name_lock(false),
    waiting_on_cond(false),
    keys_in_use(0),
    keys_for_keyread(0)
  {
    init(key, key_length, new_table_name, new_path);
  }

  /** Category of this table. */
  enum_table_category table_category;

  uint32_t open_count;			/* Number of tables in open list */

  /* The following is copied to each Table on OPEN */
  Field **field;
  Field **found_next_number_field;
  Field *timestamp_field;               /* Used only during open */
  KEY  *key_info;			/* data of keys in database */
  uint	*blob_field;			/* Index to blobs in Field arrray*/

  /* hash of field names (contains pointers to elements of field array) */
  HASH	name_hash;			/* hash of field names */
  drizzled::memory::Root mem_root;
  TYPELIB keynames;			/* Pointers to keynames */
  TYPELIB fieldnames;			/* Pointer to fieldnames */
  TYPELIB *intervals;			/* pointer to interval info */
  pthread_mutex_t mutex;                /* For locking the share  */
  pthread_cond_t cond;			/* To signal that share is ready */

  unsigned char	*default_values;		/* row with default values */
  const CHARSET_INFO *table_charset; /* Default charset of string fields */

  MyBitmap all_set;
  /*
    Key which is used for looking-up table in table cache and in the list
    of thread's temporary tables. Has the form of:
    "database_name\0table_name\0" + optional part for temporary tables.

    Note that all three 'table_cache_key', 'db' and 'table_name' members
    must be set (and be non-zero) for tables in table cache. They also
    should correspond to each other.
    To ensure this one can use set_table_cache() methods.
  */
  LEX_STRING table_cache_key;
  LEX_STRING db;                        /* Pointer to db */
  LEX_STRING table_name;                /* Table name (for open) */
  LEX_STRING path;	/* Path to .frm file (from datadir) */
  LEX_STRING normalized_path;		/* unpack_filename(path) */

  uint32_t   block_size;                   /* create information */
  uint64_t   version;
  uint32_t   timestamp_offset;		/* Set to offset+1 of record */
  uint32_t   reclength;			/* Recordlength */
  uint32_t   stored_rec_length;         /* Stored record length*/
  enum row_type row_type;		/* How rows are stored */

private:
  /* Max rows is a hint to HEAP during a create tmp table */
  uint64_t max_rows;

  drizzled::message::Table *table_proto;
public:

  /* This is only used in one location currently */
  inline void setTableProto(drizzled::message::Table *arg)
  {
    assert(table_proto == NULL);
    table_proto= arg;
  }

  inline bool hasComment()
  {
    return (table_proto) ?  table_proto->options().has_comment() : false; 
  }

  inline const char *getComment()
  {
    return (table_proto && table_proto->has_options()) ?  table_proto->options().comment().c_str() : NULL; 
  }

  inline uint32_t getCommentLength()
  {
    return (table_proto) ? table_proto->options().comment().length() : 0; 
  }

  inline bool hasKeyBlockSize()
  {
    return (table_proto) ? table_proto->options().has_key_block_size() : false;
  }

  inline uint32_t getKeyBlockSize()
  {
    return (table_proto) ? table_proto->options().key_block_size() : 0;
  }

  inline uint64_t getMaxRows()
  {
    return max_rows;
  }

  inline void setMaxRows(uint64_t arg)
  {
    max_rows= arg;
  }

  drizzled::plugin::StorageEngine *storage_engine;			/* storage engine plugin */
  inline drizzled::plugin::StorageEngine *db_type() const	/* table_type for handler */
  {
    return storage_engine;
  }
  inline drizzled::plugin::StorageEngine *getEngine() const	/* table_type for handler */
  {
    return storage_engine;
  }
  enum tmp_table_type tmp_table;

  uint32_t ref_count;       /* How many Table objects uses this */
  uint32_t null_bytes;
  uint32_t last_null_bit_pos;
  uint32_t fields;				/* Number of fields */
  uint32_t rec_buff_length;                 /* Size of table->record[] buffer */
  uint32_t keys, key_parts;
  uint32_t max_key_length, max_unique_length, total_key_length;
  uint32_t uniques;                         /* Number of UNIQUE index */
  uint32_t null_fields;			/* number of null fields */
  uint32_t blob_fields;			/* number of blob fields */
  uint32_t timestamp_field_offset;		/* Field number for timestamp field */
  uint32_t varchar_fields;                  /* number of varchar fields */
  uint32_t db_create_options;		/* Create options from database */
  uint32_t db_options_in_use;		/* Options in use */
  uint32_t db_record_offset;		/* if HA_REC_IN_SEQ */
  uint32_t rowid_field_offset;		/* Field_nr +1 to rowid field */
  /**
   * @TODO 
   *
   * Currently the replication services component uses
   * the primary_key member to determine which field is the table's
   * primary key.  However, as it exists, because this member is scalar, it
   * only supports a single-column primary key. Is there a better way
   * to ask for the fields which are in a primary key?
   */
  uint32_t primary_key;
  /* Index of auto-updated TIMESTAMP field in field array */
  uint32_t next_number_index;               /* autoincrement key number */
  uint32_t next_number_key_offset;          /* autoinc keypart offset in a key */
  uint32_t next_number_keypart;             /* autoinc keypart number in a key */
  uint32_t error, open_errno, errarg;       /* error from open_table_def() */
  uint32_t column_bitmap_size;

  uint8_t blob_ptr_size;			/* 4 or 8 */
  bool db_low_byte_first;		/* Portable row format */
  bool name_lock;
  bool replace_with_name_lock;
  bool waiting_on_cond;                 /* Protection against free */

  /*
    Set of keys in use, implemented as a Bitmap.
    Excludes keys disabled by ALTER Table ... DISABLE KEYS.
  */
  key_map keys_in_use;
  key_map keys_for_keyread;

  /*
    Set share's table cache key and update its db and table name appropriately.

    SYNOPSIS
    set_table_cache_key()
    key_buff    Buffer with already built table cache key to be
    referenced from share.
    key_length  Key length.

    NOTES
    Since 'key_buff' buffer will be referenced from share it should has same
    life-time as share itself.
    This method automatically ensures that TableShare::table_name/db have
    appropriate values by using table cache key as their source.
  */

  void set_table_cache_key(char *key_buff, uint32_t key_length)
  {
    table_cache_key.str= key_buff;
    table_cache_key.length= key_length;
    /*
      Let us use the fact that the key is "db/0/table_name/0" + optional
      part for temporary tables.
    */
    db.str=            table_cache_key.str;
    db.length=         strlen(db.str);
    table_name.str=    db.str + db.length + 1;
    table_name.length= strlen(table_name.str);
  }


  /*
    Set share's table cache key and update its db and table name appropriately.

    SYNOPSIS
    set_table_cache_key()
    key_buff    Buffer to be used as storage for table cache key
    (should be at least key_length bytes).
    key         Value for table cache key.
    key_length  Key length.

    NOTE
    Since 'key_buff' buffer will be used as storage for table cache key
    it should has same life-time as share itself.
  */

  void set_table_cache_key(char *key_buff, const char *key, uint32_t key_length)
  {
    memcpy(key_buff, key, key_length);
    set_table_cache_key(key_buff, key_length);
  }

  inline bool honor_global_locks()
  {
    return (table_category == TABLE_CATEGORY_USER);
  }


  /*
    Initialize share for temporary tables

    SYNOPSIS
    init()
    share	Share to fill
    key		Table_cache_key, as generated from create_table_def_key.
    must start with db name.
    key_length	Length of key
    table_name	Table name
    path	Path to file (possible in lower case) without .frm

    NOTES
    This is different from alloc_table_share() because temporary tables
    don't have to be shared between threads or put into the table def
    cache, so we can do some things notable simpler and faster

    If table is not put in session->temporary_tables (happens only when
    one uses OPEN TEMPORARY) then one can specify 'db' as key and
    use key_length= 0 as neither table_cache_key or key_length will be used).
  */

  void init()
  {
    init("", 0, "", "");
  }

  void init(const char *new_table_name,
            const char *new_path)
  {
    init("", 0, new_table_name, new_path);
  }

  void init(const char *key,
            uint32_t key_length, const char *new_table_name,
            const char *new_path)
  {
    memset(this, 0, sizeof(TableShare));
    init_sql_alloc(&mem_root, TABLE_ALLOC_BLOCK_SIZE, 0);
    table_category=         TABLE_CATEGORY_TEMPORARY;
    tmp_table=              INTERNAL_TMP_TABLE;
    db.str=                 (char*) key;
    db.length=		 strlen(key);
    table_cache_key.str=    (char*) key;
    table_cache_key.length= key_length;
    table_name.str=         (char*) new_table_name;
    table_name.length=      strlen(new_table_name);
    path.str=               (char*) new_path;
    normalized_path.str=    (char*) new_path;
    path.length= normalized_path.length= strlen(new_path);

    return;
  }

  /*
    Free table share and memory used by it

    SYNOPSIS
    free_table_share()
    share		Table share

    NOTES
    share->mutex must be locked when we come here if it's not a temp table
  */

  void free_table_share()
  {
    drizzled::memory::Root new_mem_root;
    assert(ref_count == 0);

    /*
      If someone is waiting for this to be deleted, inform it about this.
      Don't do a delete until we know that no one is refering to this anymore.
    */
    if (tmp_table == NO_TMP_TABLE)
    {
      /* share->mutex is locked in release_table_share() */
      while (waiting_on_cond)
      {
        pthread_cond_broadcast(&cond);
        pthread_cond_wait(&cond, &mutex);
      }
      /* No thread refers to this anymore */
      pthread_mutex_unlock(&mutex);
      pthread_mutex_destroy(&mutex);
      pthread_cond_destroy(&cond);
    }
    hash_free(&name_hash);

    storage_engine= NULL;

    delete table_proto;
    table_proto= NULL;

    /* We must copy mem_root from share because share is allocated through it */
    memcpy(&new_mem_root, &mem_root, sizeof(new_mem_root));
    free_root(&new_mem_root, MYF(0));                 // Free's share
  }

  void open_table_error(int pass_error, int db_errno, int pass_errarg);



  /*
    Create a table cache key

    SYNOPSIS
    createKey()
    key			Create key here (must be of size MAX_DBKEY_LENGTH)
    table_list		Table definition

    IMPLEMENTATION
    The table cache_key is created from:
    db_name + \0
    table_name + \0

    if the table is a tmp table, we add the following to make each tmp table
    unique on the slave:

    4 bytes for master thread id
    4 bytes pseudo thread id

    RETURN
    Length of key
  */

  static inline uint32_t createKey(char *key, std::string& db_arg,
                                   std::string& table_name_arg)
  {
    return createKey(key, db_arg.c_str(), table_name_arg.c_str());
  }

  static inline uint32_t createKey(char *key, const char *db_arg, const char *table_name_arg)
  {
    uint32_t key_length;
    char *key_pos= key;

    key_pos= strcpy(key_pos, db_arg) + strlen(db_arg);
    key_pos= strcpy(key_pos+1, table_name_arg) +
      strlen(table_name_arg);
    key_length= (uint32_t)(key_pos-key)+1;

    return key_length;
  }

  static void cacheStart(void);
  static void cacheStop(void);
  static void release(TableShare *share);
  static void release(const char *key, uint32_t key_length);
  static TableShare *getShare(const char *db, const char *table_name);
  static TableShare *getShare(Session *session, 
                              TableList *table_list, char *key,
                              uint32_t key_length, uint32_t, int *error);
};

#endif /* DRIZZLED_TABLE_SHARE_H */
