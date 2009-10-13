/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
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

/* Structs that defines the Table */

#ifndef DRIZZLED_TABLE_H
#define DRIZZLED_TABLE_H

#include <plugin/myisam/myisam.h>
#include <mysys/hash.h>
#include <mysys/my_bitmap.h>
#include "drizzled/order.h"
#include "drizzled/filesort_info.h"
#include "drizzled/natural_join_column.h"
#include "drizzled/field_iterator.h"
#include "drizzled/handler.h"
#include "drizzled/lex_string.h"
#include "drizzled/table_list.h"
#include "drizzled/table_share.h"

#include <string>

using namespace std;

class Item;
class Item_subselect;
class Select_Lex_Unit;
class Select_Lex;
class COND_EQUAL;
class Security_context;
class TableList;
class Field_timestamp;
class Field_blob;

typedef enum enum_table_category TABLE_CATEGORY;

bool create_myisam_from_heap(Session *session, Table *table,
                             MI_COLUMNDEF *start_recinfo,
                             MI_COLUMNDEF **recinfo,
                             int error, bool ignore_last_dupp_key_error);

/**
 * Class representing a set of records, either in a temporary, 
 * normal, or derived table.
 */
class Table 
{
public:

  TableShare *s; /**< Pointer to the shared metadata about the table */
  Field **field; /**< Pointer to fields collection */

  handler *file; /**< Pointer to the storage engine's handler managing this table */
  Table *next;
  Table *prev;

  MyBitmap *read_set; /* Active column sets */
  MyBitmap *write_set; /* Active column sets */

  uint32_t tablenr;
  uint32_t db_stat; /**< information about the file as in handler.h */

  MyBitmap def_read_set; /**< Default read set of columns */
  MyBitmap def_write_set; /**< Default write set of columns */
  MyBitmap tmp_set; /* Not sure about this... */

  Session	*in_use; /**< Pointer to the current session using this object */

  unsigned char *record[2]; /**< Pointer to "records" */
  unsigned char *insert_values; /* used by INSERT ... UPDATE */
  KEY  *key_info; /**< data of keys in database */
  Field *next_number_field; /**< Set if next_number is activated. @TODO What the heck is the difference between this and the next member? */
  Field *found_next_number_field; /**< Points to the "next-number" field (autoincrement field) */
  Field_timestamp *timestamp_field; /**< Points to the auto-setting timestamp field, if any */

  TableList *pos_in_table_list; /* Element referring to this table */
  order_st *group;
  const char *alias; /**< alias or table name if no alias */
  unsigned char *null_flags;

  uint32_t lock_position; /**< Position in DRIZZLE_LOCK.table */
  uint32_t lock_data_start; /**< Start pos. in DRIZZLE_LOCK.locks */
  uint32_t lock_count; /**< Number of locks */
  uint32_t used_fields;
  uint32_t status; /* What's in record[0] */
  /* number of select if it is derived table */
  uint32_t derived_select_number;
  int	current_lock; /**< Type of lock on table */
  bool copy_blobs; /**< Should blobs by copied when storing? */

  /*
    0 or JOIN_TYPE_{LEFT|RIGHT}. Currently this is only compared to 0.
    If maybe_null !=0, this table is inner w.r.t. some outer join operation,
    and null_row may be true.
  */
  bool maybe_null;

  /*
    If true, the current table row is considered to have all columns set to
    NULL, including columns declared as "not null" (see maybe_null).
  */
  bool null_row;

  bool force_index;
  bool distinct,const_table,no_rows;
  bool key_read;
  bool no_keyread;
  /*
    Placeholder for an open table which prevents other connections
    from taking name-locks on this table. Typically used with
    TableShare::version member to take an exclusive name-lock on
    this table name -- a name lock that not only prevents other
    threads from opening the table, but also blocks other name
    locks. This is achieved by:
    - setting open_placeholder to 1 - this will block other name
      locks, as wait_for_locked_table_name will be forced to wait,
      see table_is_used for details.
    - setting version to 0 - this will force other threads to close
      the instance of this table and wait (this is the same approach
      as used for usual name locks).
    An exclusively name-locked table currently can have no handler
    object associated with it (db_stat is always 0), but please do
    not rely on that.
  */
  bool open_placeholder;
  bool locked_by_name;
  bool no_cache;
  /*
    To indicate that a non-null value of the auto_increment field
    was provided by the user or retrieved from the current record.
    Used only in the MODE_NO_AUTO_VALUE_ON_ZERO mode.
  */
  bool auto_increment_field_not_null;
  bool alias_name_used; /* true if table_name is alias */

  /*
   The ID of the query that opened and is using this table. Has different
   meanings depending on the table type.

   Temporary tables:

   table->query_id is set to session->query_id for the duration of a statement
   and is reset to 0 once it is closed by the same statement. A non-zero
   table->query_id means that a statement is using the table even if it's
   not the current statement (table is in use by some outer statement).

   Non-temporary tables:

   Under pre-locked or LOCK TABLES mode: query_id is set to session->query_id
   for the duration of a statement and is reset to 0 once it is closed by
   the same statement. A non-zero query_id is used to control which tables
   in the list of pre-opened and locked tables are actually being used.
  */
  query_id_t	query_id;

  /*
    Estimate of number of records that satisfy SARGable part of the table
    condition, or table->file->records if no SARGable condition could be
    constructed.
    This value is used by join optimizer as an estimate of number of records
    that will pass the table condition (condition that depends on fields of
    this table and constants)
  */
  ha_rows quick_condition_rows;

  /*
    If this table has TIMESTAMP field with auto-set property (pointed by
    timestamp_field member) then this variable indicates during which
    operations (insert only/on update/in both cases) we should set this
    field to current timestamp. If there are no such field in this table
    or we should not automatically set its value during execution of current
    statement then the variable contains TIMESTAMP_NO_AUTO_SET (i.e. 0).

    Value of this variable is set for each statement in open_table() and
    if needed cleared later in statement processing code (see mysql_update()
    as example).
  */
  timestamp_auto_set_type timestamp_field_type;
  table_map	map; /* ID bit of table (1,2,4,8,16...) */

  RegInfo reginfo; /* field connections */

  /*
    Map of keys that can be used to retrieve all data from this table
    needed by the query without reading the row.
  */
  key_map covering_keys;
  key_map quick_keys;
  key_map merge_keys;

  /*
    A set of keys that can be used in the query that references this
    table.

    All indexes disabled on the table's TableShare (see Table::s) will be
    subtracted from this set upon instantiation. Thus for any Table t it holds
    that t.keys_in_use_for_query is a subset of t.s.keys_in_use. Generally we
    must not introduce any new keys here (see setup_tables).

    The set is implemented as a bitmap.
  */
  key_map keys_in_use_for_query;
  /* Map of keys that can be used to calculate GROUP BY without sorting */
  key_map keys_in_use_for_group_by;
  /* Map of keys that can be used to calculate ORDER BY without sorting */
  key_map keys_in_use_for_order_by;

  /*
    For each key that has quick_keys.test(key) == true: estimate of #records
    and max #key parts that range access would use.
  */
  ha_rows	quick_rows[MAX_KEY];

  /* Bitmaps of key parts that =const for the entire join. */
  key_part_map  const_key_parts[MAX_KEY];

  uint32_t quick_key_parts[MAX_KEY];
  uint32_t quick_n_ranges[MAX_KEY];

  MEM_ROOT mem_root;
  filesort_info_st sort;

  Table() : 
    s(NULL), 
    field(NULL),
    file(NULL),
    next(NULL),
    prev(NULL),
    read_set(NULL),
    write_set(NULL),
    tablenr(0),
    db_stat(0),
    in_use(NULL),
    insert_values(NULL),
    key_info(NULL),
    next_number_field(NULL),
    found_next_number_field(NULL),
    timestamp_field(NULL),
    pos_in_table_list(NULL),
    group(NULL),
    alias(NULL),
    null_flags(NULL),
    lock_position(0),
    lock_data_start(0),
    lock_count(0),
    used_fields(0),
    status(0),
    derived_select_number(0),
    current_lock(F_UNLCK),
    copy_blobs(false),
    maybe_null(false),
    null_row(false),
    force_index(false),
    distinct(false),
    const_table(false),
    no_rows(false),
    key_read(false),
    no_keyread(false),
    open_placeholder(false),
    locked_by_name(false),
    no_cache(false),
    auto_increment_field_not_null(false),
    alias_name_used(false),
    query_id(0), 
    quick_condition_rows(0),
    timestamp_field_type(TIMESTAMP_NO_AUTO_SET),
    map(0)
  {
    record[0]= (unsigned char *) 0;
    record[1]= (unsigned char *) 0;

    covering_keys.reset();

    quick_keys.reset();
    merge_keys.reset();

    keys_in_use_for_query.reset();
    keys_in_use_for_group_by.reset();
    keys_in_use_for_order_by.reset();

    memset(quick_rows, 0, sizeof(query_id_t) * MAX_KEY);
    memset(const_key_parts, 0, sizeof(ha_rows) * MAX_KEY);

    memset(quick_key_parts, 0, sizeof(unsigned int) * MAX_KEY);
    memset(quick_n_ranges, 0, sizeof(unsigned int) * MAX_KEY);

    init_sql_alloc(&mem_root, TABLE_ALLOC_BLOCK_SIZE, 0);
    memset(&sort, 0, sizeof(filesort_info_st));
  }

  int report_error(int error);
  int closefrm(bool free_share);

  void resetTable(Session *session, TableShare *share, uint32_t db_stat_arg)
  {
    s= share;
    field= NULL;

    file= NULL;
    next= NULL;
    prev= NULL;

    read_set= NULL;
    write_set= NULL;

    tablenr= 0;
    db_stat= db_stat_arg;

    in_use= session;
    record[0]= (unsigned char *) 0;
    record[1]= (unsigned char *) 0;

    insert_values= NULL;
    key_info= NULL;
    next_number_field= NULL;
    found_next_number_field= NULL;
    timestamp_field= NULL;

    pos_in_table_list= NULL;
    group= NULL;
    alias= NULL;
    null_flags= NULL;
     
    lock_position= 0;
    lock_data_start= 0;
    lock_count= 0;
    used_fields= 0;
    status= 0;
    derived_select_number= 0;
    current_lock= F_UNLCK;
    copy_blobs= false;

    maybe_null= false;

    null_row= false;

    force_index= false;
    distinct= false;
    const_table= false;
    no_rows= false;
    key_read= false;
    no_keyread= false;

    open_placeholder= false;
    locked_by_name= false;
    no_cache= false;

    auto_increment_field_not_null= false;
    alias_name_used= false;
    
    query_id= 0;
    quick_condition_rows= 0;
     
    timestamp_field_type= TIMESTAMP_NO_AUTO_SET;
    map= 0;

    reginfo.reset();

    covering_keys.reset();

    quick_keys.reset();
    merge_keys.reset();

    keys_in_use_for_query.reset();
    keys_in_use_for_group_by.reset();
    keys_in_use_for_order_by.reset();

    memset(quick_rows, 0, sizeof(query_id_t) * MAX_KEY);
    memset(const_key_parts, 0, sizeof(ha_rows) * MAX_KEY);

    memset(quick_key_parts, 0, sizeof(unsigned int) * MAX_KEY);
    memset(quick_n_ranges, 0, sizeof(unsigned int) * MAX_KEY);

    init_sql_alloc(&mem_root, TABLE_ALLOC_BLOCK_SIZE, 0);
    memset(&sort, 0, sizeof(filesort_info_st));
  }

  /* SHARE methods */
  inline TableShare *getShare() { return s; } /* Get rid of this long term */
  inline void setShare(TableShare *new_share) { s= new_share; } /* Get rid of this long term */
  inline uint32_t sizeKeys() { return s->keys; }
  inline uint32_t sizeFields() { return s->fields; }
  inline uint32_t getRecordLength() { return s->reclength; }
  inline uint32_t sizeBlobFields() { return s->blob_fields; }
  inline uint32_t *getBlobField() { return s->blob_field; }
  inline uint32_t getNullBytes() { return s->null_bytes; }
  inline uint32_t getNullFields() { return s->null_fields; }
  inline unsigned char *getDefaultValues() { return s->default_values; }

  inline bool isDatabaseLowByteFirst() { return s->db_low_byte_first; } /* Portable row format */
  inline bool isCrashed() { return s->crashed; }
  inline bool isNameLock() { return s->name_lock; }
  inline bool isReplaceWithNameLock() { return s->replace_with_name_lock; }
  inline bool isWaitingOnCondition() { return s->waiting_on_cond; } /* Protection against free */

  /* For TMP tables, should be pulled out as a class */
  void updateCreateInfo(HA_CREATE_INFO *create_info,
                        drizzled::message::Table *table_proto);
  void setup_tmp_table_column_bitmaps(unsigned char *bitmaps);
  bool create_myisam_tmp_table(KEY *keyinfo,
                               MI_COLUMNDEF *start_recinfo,
                               MI_COLUMNDEF **recinfo,
                               uint64_t options);
  void free_tmp_table(Session *session);
  bool open_tmp_table();
  size_t max_row_length(const unsigned char *data);
  uint32_t find_shortest_key(const key_map *usable_keys);
  bool compare_record(Field **ptr);
  bool compare_record();
  /* TODO: the (re)storeRecord's may be able to be further condensed */
  void storeRecord();
  void storeRecordAsInsert();
  void storeRecordAsDefault();
  void restoreRecord();
  void restoreRecordAsDefault();
  void emptyRecord();

  /* See if this can be blown away */
  inline uint32_t getDBStat () { return db_stat; }
  inline uint32_t setDBStat () { return db_stat; }
  bool fill_item_list(List<Item> *item_list) const;
  void clear_column_bitmaps(void);
  void prepare_for_position(void);
  void mark_columns_used_by_index_no_reset(uint32_t index, MyBitmap *map);
  void mark_columns_used_by_index_no_reset(uint32_t index);
  void mark_columns_used_by_index(uint32_t index);
  void restore_column_maps_after_mark_index();
  void mark_auto_increment_column(void);
  void mark_columns_needed_for_update(void);
  void mark_columns_needed_for_delete(void);
  void mark_columns_needed_for_insert(void);
  inline void column_bitmaps_set(MyBitmap *read_set_arg,
                                 MyBitmap *write_set_arg)
  {
    read_set= read_set_arg;
    write_set= write_set_arg;
  }
  /**
   * Find field in table, no side effects, only purpose is to check for field
   * in table object and get reference to the field if found.
   *
   * @param Name of field searched for
   *
   * @retval
   *  0 field is not found
   * @retval
   *  non-0 pointer to field
   */
  Field *find_field_in_table_sef(const char *name);

  void restore_column_map(my_bitmap_map *old);

  my_bitmap_map *use_all_columns(MyBitmap *bitmap);
  inline void use_all_columns()
  {
    column_bitmaps_set(&s->all_set, &s->all_set);
  }

  inline void default_column_bitmaps()
  {
    read_set= &def_read_set;
    write_set= &def_write_set;
  }

  /* Both of the below should go away once we can move this bit to the field objects */
  inline bool isReadSet(uint32_t index)
  {
    return read_set->isBitSet(index);
  }

  inline void setReadSet(uint32_t index)
  {
    read_set->setBit(index);
  }

  inline void setReadSet()
  {
    read_set->setAll();
  }

  inline void clearReadSet(uint32_t index)
  {
    read_set->clearBit(index);
  }

  inline void clearReadSet()
  {
    read_set->clearAll();
  }

  inline bool isWriteSet(uint32_t index)
  {
    return write_set->isBitSet(index);
  }

  inline void setWriteSet(uint32_t index)
  {
    write_set->setBit(index);
  }

  inline void setWriteSet()
  {
    write_set->setAll();
  }

  inline void clearWriteSet(uint32_t index)
  {
    write_set->clearBit(index);
  }

  inline void clearWriteSet()
  {
    write_set->clearAll();
  }

  /* Is table open or should be treated as such by name-locking? */
  inline bool is_name_opened()
  {
    return db_stat || open_placeholder;
  }
  /*
    Is this instance of the table should be reopen or represents a name-lock?
  */
  inline bool needs_reopen_or_name_lock()
  { 
    return s->version != refresh_version;
  }

  /**
    clean/setup table fields and map.

    @param table        Table structure pointer (which should be setup)
    @param table_list   TableList structure pointer (owner of Table)
    @param tablenr     table number
  */
  void setup_table_map(TableList *table_list, uint32_t tablenr);
  inline void mark_as_null_row()
  {
    null_row= 1;
    status|= STATUS_NULL_ROW;
    memset(null_flags, 255, s->null_bytes);
  }

  bool rename_temporary_table(const char *db, const char *table_name);
  void free_io_cache();
  void filesort_free_buffers(bool full= false);
  void intern_close_table();
};

Table *create_virtual_tmp_table(Session *session, List<CreateField> &field_list);

typedef struct st_foreign_key_info
{
  LEX_STRING *forein_id;
  LEX_STRING *referenced_db;
  LEX_STRING *referenced_table;
  LEX_STRING *update_method;
  LEX_STRING *delete_method;
  LEX_STRING *referenced_key_name;
  List<LEX_STRING> foreign_fields;
  List<LEX_STRING> referenced_fields;
} FOREIGN_KEY_INFO;



class TableList;

#define JOIN_TYPE_LEFT	1
#define JOIN_TYPE_RIGHT	2

struct st_lex;
class select_union;
class Tmp_Table_Param;

typedef struct st_changed_table_list
{
  struct	st_changed_table_list *next;
  char		*key;
  uint32_t key_length;
} CHANGED_TableList;

struct open_table_list_st
{
  string	db;
  string	table;
  uint32_t in_use;
  uint32_t locked;

  open_table_list_st() :
    in_use(0),
    locked(0)
  { }

};

#endif /* DRIZZLED_TABLE_H */
