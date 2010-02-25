/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 - 2010 Toru Maesaka
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

#ifndef PLUGIN_BLITZDB_HA_BLITZ_H
#define PLUGIN_BLITZDB_HA_BLITZ_H

#include "config.h"
#include "drizzled/session.h"
#include "drizzled/cursor.h"
#include "drizzled/table.h"
#include "drizzled/field.h"
#include "drizzled/field/blob.h"
#include "drizzled/atomics.h"
#include "drizzled/error.h"
#include "drizzled/gettext.h"
#include "drizzled/cached_directory.h"
#include <tchdb.h>
#include <tcbdb.h>

#include <string>

#define BLITZ_DATA_EXT        ".bzd"
#define BLITZ_INDEX_EXT       ".bzx"
#define BLITZ_SYSTEM_EXT      ".bzs"
#define BLITZ_LOCK_SLOTS      16
#define BLITZ_MAX_INDEX       1
#define BLITZ_MAX_META_LEN    128
#define BLITZ_MAX_ROW_STACK   2048
#define BLITZ_MAX_KEY_LEN     1024 

using namespace std;

const string BLITZ_TABLE_PROTO_KEY = "table_definition";
const string BLITZ_TABLE_PROTO_COMMENT_KEY = "table_definition_comment";

/* Callback function for TC's B+Tree key comparison. */
extern int blitz_keycmp_cb(const char *a, int alen,
                           const char *b, int blen, void *opaque);

static const char *ha_blitz_exts[] = {
  BLITZ_DATA_EXT,
  BLITZ_INDEX_EXT,
  BLITZ_SYSTEM_EXT,
  NULL
};

/* Multi Reader-Writer lock responsible for controlling concurrency
   at the handler level. This class is implemented in blitzlock.cc */
class BlitzLock {
private:
  int scanner_count;
  int updater_count;
  pthread_cond_t condition;
  pthread_mutex_t mutex;
  pthread_mutex_t slots[BLITZ_LOCK_SLOTS];

public:
  BlitzLock();
  ~BlitzLock();

  /* Slotted Lock Mechanism for Concurrently and Atomically
     updating the index and data dictionary at the same time. */
  uint32_t slot_id(const void *data, size_t len);
  int slotted_lock(const uint32_t slot_id);
  int slotted_unlock(const uint32_t slot_id);

  /* Multi Reader-Writer Lock Mechanism. */
  void update_begin();
  void update_end();
  void scan_begin();
  void scan_end();
  void scan_update_begin();
  void scan_update_end();
};

/* Handler that takes care of all I/O to the data dictionary
   that holds actual rows. */
class BlitzData {
private:
  TCHDB *data_table;    /* Where the actual row data lives */
  TCHDB *system_table;  /* Keeps track of system info */
  char *tc_meta_buffer; /* Tokyo Cabinet's Persistent Meta Buffer */
  drizzled::atomic<uint64_t> current_hidden_row_id;

public:
  BlitzData() { current_hidden_row_id = 0; }
  ~BlitzData() {}
  int startup(const char *table_path);
  int shutdown(void);

  /* DATA DICTIONARY CREATION RELATED */
  int create_data_table(drizzled::message::Table &proto,
                        drizzled::Table &table, const char *path);
  int open_data_table(const char *path, const int mode);
  int close_data_table(void);
  bool rename_table(const char *from, const char *to);

  /* DATA DICTIONARY METADATA RELATED */
  uint64_t nrecords(void);
  uint64_t table_size(void);
  uint64_t read_meta_row_id(void);
  uint64_t read_meta_autoinc(void);
  uint32_t read_meta_keycount(void);
  void write_meta_row_id(uint64_t row_id);
  void write_meta_autoinc(uint64_t num);
  void write_meta_keycount(uint32_t nkeys);

  /* DATA DICTIONARY READ RELATED*/
  char *get_row(const char *key, const size_t klen, int *value_len);
  char *next_key_and_row(const char *key, const size_t klen,
                         int *next_key_len, const char **value,
                         int *value_len);
  char *first_row(int *row_len);

  /* DATA DICTIONARY WRITE RELATED */
  uint64_t next_hidden_row_id(void);
  int write_row(const char *key, const size_t klen,
                const unsigned char *row, const size_t rlen);
  int write_unique_row(const char *key, const size_t klen,
                       const unsigned char *row, const size_t rlen);
  bool delete_row(const char *key, const size_t klen);
  bool delete_all_rows(void);

  /* SYSTEM TABLE RELATED */
  int create_system_table(const char *path);
  int open_system_table(const char *path, const int mode);
  int close_system_table(void);
  bool write_table_definition(drizzled::message::Table &proto);
  char *get_system_entry(const char *key, const size_t klen, int *vlen);
};

/* This class is only used by the BlitzTree object which has a long life
   span. In general we use the Cursor's local KEY_PART_INFO array for
   obtaining key information. We create our own array of key information
   because there is no guarantee that the pointer to the internal key_info
   array will always be alive. */
class BlitzKeyPart {
public:
  BlitzKeyPart() : offset(0), null_pos(0), flag(0), length(0), type(0),
                   null_bitmask(0) {}
  ~BlitzKeyPart() {}

  uint32_t offset;      /* Offset of the key in the row */
  uint32_t null_pos;    /* Offset of the NULL indicator in the row */
  uint16_t flag;
  uint16_t length;      /* Length of the key */
  uint8_t type;         /* Type of the key */
  uint8_t null_bitmask; /* Bitmask to test for NULL */
};

/* Class that reprensents a BTREE index. Takes care of all I/O
   to the b+tree index structure */
class BlitzTree {
private:
  TCBDB *btree;
  BDBCUR *bt_cursor;
  char *keybuf;
  size_t keybuf_len;

public:
  BlitzTree() : bt_cursor(NULL), keybuf_len(BLITZ_MAX_KEY_LEN), length(0),
                nparts(0), type(0), cursor_moved(false), unique(false) {}
  ~BlitzTree() {}

  /* METADATA */
  BlitzKeyPart *parts; /* Array of Key Part(s) */
  int length;          /* Length of the entire key */
  int nparts;          /* Number of parts in this key */
  int type;            /* Type of the key */
  bool cursor_moved;   /* Cursor was unexplicitly moved forward */
  bool unique;         /* Whether this key is unique */

  /* BTREE INDEX CREATION RELATED */
  int open(const char *path, const int key_num, int mode);
  int create(const char *path, const int key_num);
  int drop(const char *path, const int key_num);
  int rename(const char *from, const char *to, const int key_num);
  int close(void);
  int create_cursor(void);
  void destroy_cursor(void);

  /* BTREE INDEX WRITE RELATED */
  int write(const char *key, const size_t klen, const char *val,
            const size_t vlen);
  int write_unique(const char *key, const size_t klen, const char *val,
                   const size_t vlen);
  int delete_key(void);
  int delete_all(void);

  /* BTREE INDEX READ RELATED */
  char *first_key(int *key_len);
  char *final_key(int *key_len);
  char *next_key(int *key_len);
  char *prev_key(int *key_key);
  
  /* BTREE METADATA RELATED */
  uint64_t records(void); 
};

/* Object shared among all worker threads. Try to only add
   data that will not be updated at runtime or those that
   do not require locking. */
class BlitzShare {
public:
  BlitzShare() : blitz_lock(), use_count(0), nkeys(0) {}
  ~BlitzShare() {}

  drizzled::atomic<uint64_t> auto_increment_value;

  BlitzLock blitz_lock;    /* Handler level lock for BlitzDB */
  BlitzData dict;          /* Utility class of BlitzDB */
  BlitzTree *btrees;       /* Array of BTREE indexes */
  std::string table_name;  /* Name and Length of the table */
  uint32_t use_count;      /* Reference counter of this object */
  uint32_t nkeys;          /* Number of indexes in this table */
  bool fixed_length_table; /* Whether the table is fixed length */
  bool primary_key_exists; /* Whether a PK exists in this table */
};

class ha_blitz: public drizzled::Cursor {
private:
  BlitzShare *share;            /* Shared object among all threads */
  drizzled::THR_LOCK_DATA lock; /* Drizzle Lock */

  /* THREAD STATE */
  bool table_scan;           /* Whether a table scan is occuring */
  bool thread_locked;        /* Whether the thread is locked */
  uint32_t sql_command_type; /* Type of SQL command to process */

  /* RECORDED KEY IN TABLE OR INDEX READ */
  char *held_key;            /* Points to held_key_buf or an allocated key */
  char *held_key_buf;        /* Buffer used to copy an unallocated key */
  int held_key_len;          /* Length of the key being held */

  /* TABLE SCANNER VARIABLES */
  char *current_key;         /* Current key in table scan */
  const char *current_row;   /* Current row in table scan */
  int current_key_len;       /* Length of the current key */
  int current_row_len;       /* Length of the current row */

  /* ROW PROCESSING VARIABLES */
  char *key_buffer;                               /* Key generation buffer */
  unsigned char pack_buffer[BLITZ_MAX_ROW_STACK]; /* Pack Buffer */
  unsigned char *secondary_row_buffer;            /* For big rows */
  size_t secondary_row_buffer_size;               /* Reserved buffer size */
  int errkey_id;

public:
  ha_blitz(drizzled::plugin::StorageEngine &engine_arg,
           drizzled::TableShare &table_arg);
  ~ha_blitz() {}

  /* TABLE CONTROL RELATED FUNCTIONS */
  const char **bas_ext() const;
  const char *index_type(uint32_t key_num);
  int open(const char *name, int mode, uint32_t open_options);
  int close(void);
  int info(uint32_t flag);

  /* TABLE SCANNER RELATED FUNCTIONS */
  int rnd_init(bool scan);
  int rnd_next(unsigned char *buf);
  int rnd_end(void);
  int rnd_pos(unsigned char *buf, unsigned char *pos);

  void position(const unsigned char *record);

  /* INDEX RELATED FUNCTIONS */
  int index_init(uint32_t key_num, bool sorted);
  int index_first(unsigned char *buf);
  int index_next(unsigned char *buf);
  int index_prev(unsigned char *buf);
  int index_last(unsigned char *buf);
  int index_read(unsigned char *buf, const unsigned char *key,
                 uint32_t key_len, enum drizzled::ha_rkey_function find_flag);
  int index_read_idx(unsigned char *buf, uint32_t key_num,
                     const unsigned char *key, uint32_t key_len,
                     enum drizzled::ha_rkey_function find_flag);
  int index_end(void);

  /* UPDATE RELATED FUNCTIONS */
  int write_row(unsigned char *buf);
  int update_row(const unsigned char *old_data, unsigned char *new_data);
  int delete_row(const unsigned char *buf);
  int delete_all_rows(void);
  virtual void get_auto_increment(uint64_t offset, uint64_t increment,
                                  uint64_t nb_desired_values,
                                  uint64_t *first_value,
                                  uint64_t *nb_reserved_values);
  int reset_auto_increment(uint64_t value);

  /* UTILITY FUNCTIONS (BLITZDB SPECIFIC) */
  BlitzShare *get_share(const char *table_name);

  /* LOCK RELATED FUNCTIONS (BLITZDB SPECIFIC) */
  int critical_section_enter();
  int critical_section_exit();
  uint32_t max_row_length(void);

  /* INDEX KEY RELATED FUNCTIONS (BLITZDB SPECIFIC) */
  size_t make_primary_key(char *pack_to, const unsigned char *row);
  size_t make_index_key(char *pack_to, int key_num, const unsigned char *row);
  size_t btree_key_length(const char *key, const int key_num);
  char *native_to_blitz_key(const unsigned char *native_key,
                            const int key_num, int *return_key_length);
  void keep_track_of_key(const char *key, const int klen);

  /* ROW RELATED FUNCTIONS (BLITZDB SPECIFIC) */
  size_t pack_row(unsigned char *row_buffer, unsigned char *row_to_pack);
  bool unpack_row(unsigned char *to, const char *from, const size_t from_len);
  unsigned char *get_pack_buffer(const size_t size);

  /* COMAPARISON LOGIC (BLITZDB SPECIFIC) */
  int compare_rows_for_unique_violation(const unsigned char *old_row,
                                        const unsigned char *new_row);
};

class BlitzEngine : public drizzled::plugin::StorageEngine {
public:
  BlitzEngine(const string &name_arg)
    : drizzled::plugin::StorageEngine(name_arg,
                                      drizzled::HTON_FILE_BASED |
                                      drizzled::HTON_NULL_IN_KEY |
                                      drizzled::HTON_STATS_RECORDS_IS_EXACT |
                                      drizzled::HTON_SKIP_STORE_LOCK) {
    table_definition_ext = BLITZ_SYSTEM_EXT;
  }

  virtual drizzled::Cursor *create(drizzled::TableShare &table,
                                   drizzled::memory::Root *mem_root) {
    return new (mem_root) ha_blitz(*this, table);
  }

  const char **bas_ext() const {
    return ha_blitz_exts;
  }

  int doCreateTable(drizzled::Session *session, const char *table_name,
                    drizzled::Table &table_arg, drizzled::message::Table&);

  int doRenameTable(drizzled::Session *session, const char *from, const char *to);

  int doDropTable(drizzled::Session&, const string table_name); 

  int doGetTableDefinition(drizzled::Session& session,
                           const char *path, const char *db,
                           const char *table_name, const bool is_tmp,
                           drizzled::message::Table *table_proto);

  void doGetTableNames(drizzled::CachedDirectory &directory, string&,
                       set<string>& set_of_names);

  uint32_t max_supported_keys() const { return BLITZ_MAX_INDEX; }
  uint32_t max_supported_key_length() const { return BLITZ_MAX_KEY_LEN; }
  uint32_t max_supported_key_part_length() const { return BLITZ_MAX_KEY_LEN; }

  uint32_t index_flags(enum drizzled::ha_key_alg) const {
    return (HA_READ_NEXT |
            HA_READ_PREV |
            HA_READ_ORDER |
            HA_KEYREAD_ONLY);
  }
};

#endif /* PLUGIN_BLITZDB_HA_BLITZ_H */
