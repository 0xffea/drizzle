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

#include <boost/unordered_map.hpp>

#include "drizzled/typelib.h"
#include "drizzled/my_hash.h"
#include "drizzled/memory/root.h"
#include "drizzled/message/table.pb.h"

namespace drizzled
{

typedef boost::unordered_map< TableIdentifier::Key, TableShare *> TableDefinitionCache;

const static std::string STANDARD_STRING("STANDARD");
const static std::string TEMPORARY_STRING("TEMPORARY");
const static std::string INTERNAL_STRING("INTERNAL");
const static std::string FUNCTION_STRING("FUNCTION");

namespace plugin
{
class EventObserverList;
}

class Field_blob;

class TableShare
{
  typedef std::vector<std::string> StringVector;
public:
  TableShare(TableIdentifier::Type type_arg);

  TableShare(TableIdentifier &identifier, const TableIdentifier::Key &key); // Used by placeholder

  TableShare(const TableIdentifier &identifier); // Just used during createTable()

  TableShare(TableIdentifier::Type type_arg,
             TableIdentifier &identifier,
             char *path_arg= NULL, uint32_t path_length_arg= 0); // Shares for cache

  ~TableShare();

private:
  /** Category of this table. */
  enum_table_category table_category;

  uint32_t open_count;			/* Number of tables in open list */
public:

  bool isTemporaryCategory() const
  {
    return (table_category == TABLE_CATEGORY_TEMPORARY);
  }

  void setTableCategory(enum_table_category arg)
  {
    table_category= arg;
  }

  /* The following is copied to each Table on OPEN */
  typedef std::vector<Field *> Fields;
private:
  Fields field;
public:
  const Fields getFields() const
  {
    return field;
  }

  Field ** getFields(bool)
  {
    return &field[0];
  }

  void setFields(uint32_t arg)
  {
    field.resize(arg);
  }

  uint32_t positionFields(Field **arg) const
  {
    return (arg - (Field **)&field[0]);
  }

  void pushField(Field *arg)
  {
    fields++;
    field.push_back(arg);
  }


  Field **found_next_number_field;
private:
  Field *timestamp_field;               /* Used only during open */
public:

  Field *getTimestampField() const               /* Used only during open */
  {
    return timestamp_field;
  }

  void setTimestampField(Field *arg) /* Used only during open */
  {
    timestamp_field= arg;
  }


private:
  KeyInfo  *key_info;			/* data of keys in database */
public:
  KeyInfo &getKeyInfo(uint32_t arg) const
  {
    return key_info[arg];
  }
  std::vector<uint>	blob_field;			/* Index to blobs in Field arrray*/

  /* hash of field names (contains pointers to elements of field array) */
  HASH	name_hash;			/* hash of field names */
private:
  memory::Root mem_root;
public:
  void *alloc_root(size_t arg)
  {
    return mem_root.alloc_root(arg);
  }

  char *strmake_root(const char *str_arg, size_t len_arg)
  {
    return mem_root.strmake_root(str_arg, len_arg);
  }

  memory::Root *getMemRoot()
  {
    return &mem_root;
  }

private:
  std::vector<std::string> _keynames;

  void addKeyName(std::string arg)
  {
    std::transform(arg.begin(), arg.end(),
                   arg.begin(), ::toupper);
    _keynames.push_back(arg);
  }
public:
  bool doesKeyNameExist(const char *name_arg, uint32_t name_length, uint32_t &position) const
  {
    std::string arg(name_arg, name_length);
    std::transform(arg.begin(), arg.end(),
                   arg.begin(), ::toupper);

    std::vector<std::string>::const_iterator iter= std::find(_keynames.begin(), _keynames.end(), arg);

    if (iter == _keynames.end())
      return false;

    position= iter -  _keynames.begin();

    return true;
  }

  bool doesKeyNameExist(std::string arg, uint32_t &position) const
  {
    std::transform(arg.begin(), arg.end(),
                   arg.begin(), ::toupper);

    std::vector<std::string>::const_iterator iter= std::find(_keynames.begin(), _keynames.end(), arg);

    if (iter == _keynames.end())
    {
      position= -1; //historical, required for finding primary key from unique
      return false;
    }

    position= iter -  _keynames.begin();

    return true;
  }

private:
  std::vector<TYPELIB> intervals;			/* pointer to interval info */

public:
  pthread_mutex_t mutex;                /* For locking the share  */
  pthread_cond_t cond;			/* To signal that share is ready */

private:
  std::vector<unsigned char> default_values;		/* row with default values */
public:
  unsigned char * getDefaultValues()
  {
    return &default_values[0];
  }
  void resizeDefaultValues(size_t arg)
  {
    default_values.resize(arg);
  }

  const CHARSET_INFO *table_charset; /* Default charset of string fields */

  MyBitmap all_set;
private:
  std::vector<my_bitmap_map> all_bitmap;

public:
  /*
    Key which is used for looking-up table in table cache and in the list
    of thread's temporary tables. Has the form of:
    "database_name\0table_name\0" + optional part for temporary tables.

    Note that all three 'table_cache_key', 'db' and 'table_name' members
    must be set (and be non-zero) for tables in table cache. They also
    should correspond to each other.
    To ensure this one can use set_table_cache() methods.
  */
private:
  TableIdentifier::Key private_key_for_cache; // This will not exist in the final design.
  std::vector<char> private_normalized_path; // This will not exist in the final design.
  LEX_STRING db;                        /* Pointer to db */
  LEX_STRING table_name;                /* Table name (for open) */
  LEX_STRING path;	/* Path to table (from datadir) */
  LEX_STRING normalized_path;		/* unpack_filename(path) */
public:

  const char *getNormalizedPath() const
  {
    return normalized_path.str;
  }

  const char *getPath() const
  {
    return path.str;
  }

  const TableIdentifier::Key& getCacheKey() const // This should never be called when we aren't looking at a cache.
  {
    assert(private_key_for_cache.size());
    return private_key_for_cache;
  }

  size_t getCacheKeySize() const
  {
    return private_key_for_cache.size();
  }

  void setPath(char *str_arg, uint32_t size_arg)
  {
    path.str= str_arg;
    path.length= size_arg;
  }

  void setNormalizedPath(char *str_arg, uint32_t size_arg)
  {
    normalized_path.str= str_arg;
    normalized_path.length= size_arg;
  }

  const char *getTableName() const
  {
    return table_name.str;
  }

  uint32_t getTableNameSize() const
  {
    return table_name.length;
  }

  const std::string &getTableName(std::string &name_arg) const
  {
    name_arg.clear();
    name_arg.append(table_name.str, table_name.length);

    return name_arg;
  }

  const char *getSchemaName() const
  {
    return db.str;
  }

  const std::string &getSchemaName(std::string &schema_name_arg) const
  {
    schema_name_arg.clear();
    schema_name_arg.append(db.str, db.length);

    return schema_name_arg;
  }

  uint32_t   block_size;                   /* create information */

private:
  uint64_t   version;
public:
  uint64_t getVersion() const
  {
    return version;
  }

  void refreshVersion()
  {
   version= refresh_version;
  }

  void resetVersion()
  {
    version= 0;
  }

  uint32_t   timestamp_offset;		/* Set to offset+1 of record */
private:
  uint32_t   reclength;			/* Recordlength */
public:
  uint32_t   stored_rec_length;         /* Stored record length*/
  enum row_type row_type;		/* How rows are stored */

  uint32_t getRecordLength() const
  {
    return reclength;
  }

  void setRecordLength(uint32_t arg)
  {
    reclength= arg;
  }

  const Field_blob *getBlobFieldAt(uint32_t arg) const
  {
    if (arg < blob_fields)
      return (Field_blob*) field[blob_field[arg]];

    return NULL;
  }

private:
  /* Max rows is a hint to HEAP during a create tmp table */
  uint64_t max_rows;

  message::Table *table_proto;
public:

  /*
    @note Without a table_proto, we assume we are building a STANDARD table.
    This will be modified once we use Identifiers in the Share itself.
  */
  message::Table::TableType getTableType() const
  {
    return table_proto ? table_proto->type() : message::Table::STANDARD;
  }

  const std::string &getTableTypeAsString() const
  {
    switch (table_proto->type())
    {
    default:
    case message::Table::STANDARD:
      return STANDARD_STRING;
    case message::Table::TEMPORARY:
      return TEMPORARY_STRING;
    case message::Table::INTERNAL:
      return INTERNAL_STRING;
    case message::Table::FUNCTION:
      return FUNCTION_STRING;
    }
  }

  /* This is only used in one location currently */
  inline message::Table *getTableProto() const
  {
    return table_proto;
  }

  inline void setTableProto(message::Table *arg)
  {
    assert(table_proto == NULL);
    table_proto= arg;
  }

  inline bool hasComment() const
  {
    return (table_proto) ?  table_proto->options().has_comment() : false; 
  }

  inline const char *getComment()
  {
    return (table_proto && table_proto->has_options()) ?  table_proto->options().comment().c_str() : NULL; 
  }

  inline uint32_t getCommentLength() const
  {
    return (table_proto) ? table_proto->options().comment().length() : 0; 
  }

  inline uint64_t getMaxRows() const
  {
    return max_rows;
  }

  inline void setMaxRows(uint64_t arg)
  {
    max_rows= arg;
  }

  /**
   * Returns true if the supplied Field object
   * is part of the table's primary key.
 */
  bool fieldInPrimaryKey(Field *field) const;

  plugin::StorageEngine *storage_engine;			/* storage engine plugin */
  inline plugin::StorageEngine *db_type() const	/* table_type for handler */
  {
    return storage_engine;
  }
  inline plugin::StorageEngine *getEngine() const	/* table_type for handler */
  {
    return storage_engine;
  }

private:
  TableIdentifier::Type tmp_table;
public:

  TableIdentifier::Type getType() const
  {
    return tmp_table;
  }

private:
  uint32_t ref_count;       /* How many Table objects uses this */
public:
  uint32_t getTableCount() const
  {
    return ref_count;
  }

  void incrementTableCount()
  {
    ref_count++;
  }

  uint32_t null_bytes;
  uint32_t last_null_bit_pos;
  uint32_t fields;				/* Number of fields */

  uint32_t sizeFields() const
  {
    return fields;
  }

  uint32_t rec_buff_length;                 /* Size of table->record[] buffer */
  uint32_t keys;

  uint32_t sizeKeys() const
  {
    return keys;
  }
  uint32_t key_parts;
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
private:
  uint32_t primary_key;
public:

  uint32_t getPrimaryKey() const
  {
    return primary_key;
  }

  bool hasPrimaryKey() const
  {
    return primary_key != MAX_KEY;
  }

  /* Index of auto-updated TIMESTAMP field in field array */
  uint32_t next_number_index;               /* autoincrement key number */
  uint32_t next_number_key_offset;          /* autoinc keypart offset in a key */
  uint32_t next_number_keypart;             /* autoinc keypart number in a key */
  uint32_t error, open_errno, errarg;       /* error from open_table_def() */
  uint32_t column_bitmap_size;

  uint8_t blob_ptr_size;			/* 4 or 8 */
  bool db_low_byte_first;		/* Portable row format */

private:
  bool name_lock;
public:
  bool isNameLock() const
  {
    return name_lock;
  }

  bool replace_with_name_lock;

private:
  bool waiting_on_cond;                 /* Protection against free */
public:
  bool isWaitingOnCondition()
  {
    return waiting_on_cond;
  }

  /*
    Set of keys in use, implemented as a Bitmap.
    Excludes keys disabled by ALTER Table ... DISABLE KEYS.
  */
  key_map keys_in_use;
  key_map keys_for_keyread;

  /* 
    event_observers is a class containing all the event plugins that have 
    registered an interest in this table.
  */
  private:
  plugin::EventObserverList *event_observers;
  public:
  plugin::EventObserverList *getTableObservers() 
  { 
    return event_observers;
  }
  
  void setTableObservers(plugin::EventObserverList *observers) 
  { 
    event_observers= observers;
  }
  
  /*
    Set share's identifier information.

    SYNOPSIS
    setIdentifier()

    NOTES
  */

  void setIdentifier(TableIdentifier &identifier_arg);

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
    path	Path to table (possible in lower case)

    NOTES
    
  */

private:
  void init(const char *new_table_name,
            const char *new_path);
public:

  void open_table_error(int pass_error, int db_errno, int pass_errarg);

  static void cacheStart(void);
  static void cacheStop(void);
  static void release(TableShare *share);
  static void release(TableIdentifier &identifier);
  static const TableDefinitionCache &getCache();
  static TableShare *getShare(TableIdentifier &identifier);
  static TableShare *getShareCreate(Session *session, 
                                    TableIdentifier &identifier,
                                    int *error);

  friend std::ostream& operator<<(std::ostream& output, const TableShare &share)
  {
    output << "TableShare:(";
    output <<  share.getSchemaName();
    output << ", ";
    output << share.getTableName();
    output << ", ";
    output << share.getTableTypeAsString();
    output << ", ";
    output << share.getPath();
    output << ")";

    return output;  // for multiple << operators.
  }

  Field *make_field(unsigned char *ptr,
                    uint32_t field_length,
                    bool is_nullable,
                    unsigned char *null_pos,
                    unsigned char null_bit,
                    uint8_t decimals,
                    enum_field_types field_type,
                    const CHARSET_INFO * field_charset,
                    Field::utype unireg_check,
                    TYPELIB *interval,
                    const char *field_name);

  int open_table_def(Session& session, TableIdentifier &identifier);

  int open_table_from_share(Session *session,
                            const TableIdentifier &identifier,
                            const char *alias,
                            uint32_t db_stat, uint32_t ha_open_flags,
                            Table &outparam);
  int parse_table_proto(Session& session, message::Table &table);
private:
  int inner_parse_table_proto(Session& session, message::Table &table);
};

} /* namespace drizzled */

#endif /* DRIZZLED_TABLE_SHARE_H */
