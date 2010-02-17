/*****************************************************************************

Copyright (c) 2000, 2009, MySQL AB & Innobase Oy. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59 Temple
Place, Suite 330, Boston, MA 02111-1307 USA

*****************************************************************************/

/*
  This file is based on ha_berkeley.h of MySQL distribution

  This file defines the Innodb Cursor: the interface between MySQL and
  Innodb
*/

#ifndef INNODB_HANDLER_HA_INNODB_H
#define INNODB_HANDLER_HA_INNODB_H

#include <drizzled/cursor.h>
#include <drizzled/thr_lock.h>
#include <drizzled/plugin/transactional_storage_engine.h>

using namespace drizzled;
/** InnoDB table share */
typedef struct st_innobase_share {
	THR_LOCK	lock;		/*!< MySQL lock protecting
					this structure */
	const char*	table_name;	/*!< InnoDB table name */
	uint		use_count;	/*!< reference count,
					incremented in get_share()
					and decremented in free_share() */
	void*		table_name_hash;/*!< hash table chain node */
} INNOBASE_SHARE;


/** InnoDB B-tree index */
struct dict_index_struct;
/** Prebuilt structures in an Innobase table handle used within MySQL */
struct row_prebuilt_struct;

/** InnoDB B-tree index */
typedef struct dict_index_struct dict_index_t;
/** Prebuilt structures in an Innobase table handle used within MySQL */
typedef struct row_prebuilt_struct row_prebuilt_t;

/** The class defining a handle to an Innodb table */
class ha_innobase: public Cursor
{
	row_prebuilt_t*	prebuilt;	/*!< prebuilt struct in InnoDB, used
					to save CPU time with prebuilt data
					structures*/
	Session*	user_session;	/*!< the thread handle of the user
					currently using the handle; this is
					set in external_lock function */
	THR_LOCK_DATA	lock;
	INNOBASE_SHARE*	share;		/*!< information for MySQL
					table locking */

	unsigned char*	upd_buff;	/*!< buffer used in updates */
	unsigned char*	key_val_buff;	/*!< buffer used in converting
					search key values from MySQL format
					to Innodb format */
	ulong		upd_and_key_val_buff_len;
					/* the length of each of the previous
					two buffers */
	uint		primary_key;
	ulong		start_of_scan;	/*!< this is set to 1 when we are
					starting a table scan but have not
					yet fetched any row, else 0 */
	uint		last_match_mode;/* match mode of the latest search:
					ROW_SEL_EXACT, ROW_SEL_EXACT_PREFIX,
					or undefined */
	uint		num_write_row;	/*!< number of write_row() calls */

	UNIV_INTERN uint store_key_val_for_row(uint keynr, char* buff, 
                                   uint buff_len, const unsigned char* record);
	UNIV_INTERN void update_session(Session* session);
	UNIV_INTERN void update_session();
	UNIV_INTERN int change_active_index(uint32_t keynr);
	UNIV_INTERN int general_fetch(unsigned char* buf, uint32_t direction, uint32_t match_mode);
	UNIV_INTERN ulint innobase_lock_autoinc();
	UNIV_INTERN uint64_t innobase_peek_autoinc();
	UNIV_INTERN ulint innobase_set_max_autoinc(uint64_t auto_inc);
	UNIV_INTERN ulint innobase_reset_autoinc(uint64_t auto_inc);
	UNIV_INTERN ulint innobase_get_autoinc(uint64_t* value);
	ulint innobase_update_autoinc(uint64_t	auto_inc);
	UNIV_INTERN ulint innobase_initialize_autoinc();
	UNIV_INTERN dict_index_t* innobase_get_index(uint keynr);
 	UNIV_INTERN uint64_t innobase_get_int_col_max_value(const Field* field);

	/* Init values for the class: */
 public:
	UNIV_INTERN ha_innobase(plugin::StorageEngine &engine,
                                TableShare &table_arg);
	UNIV_INTERN ~ha_innobase();
  /**
   * Returns the plugin::TransactionStorageEngine pointer
   * of the cursor's underlying engine.
   *
   * @todo
   *
   * Have a TransactionalCursor subclass...
   */
  UNIV_INTERN plugin::TransactionalStorageEngine *getTransactionalEngine()
  {
    return static_cast<plugin::TransactionalStorageEngine *>(engine);
  }

	/*
	  Get the row type from the storage engine.  If this method returns
	  ROW_TYPE_NOT_USED, the information in HA_CREATE_INFO should be used.
	*/
	UNIV_INTERN enum row_type get_row_type() const;

	UNIV_INTERN const char* index_type(uint key_number);
	UNIV_INTERN const key_map* keys_to_use_for_scanning();

	UNIV_INTERN int open(const char *name, int mode, uint test_if_locked);
	UNIV_INTERN int close(void);
	UNIV_INTERN double scan_time();
	UNIV_INTERN double read_time(uint index, uint ranges, ha_rows rows);

	UNIV_INTERN int write_row(unsigned char * buf);
	UNIV_INTERN int update_row(const unsigned char * old_data, unsigned char * new_data);
	UNIV_INTERN int delete_row(const unsigned char * buf);
	UNIV_INTERN bool was_semi_consistent_read();
	UNIV_INTERN void try_semi_consistent_read(bool yes);
	UNIV_INTERN void unlock_row();

	UNIV_INTERN int index_init(uint index, bool sorted);
	UNIV_INTERN int index_end();
	UNIV_INTERN int index_read(unsigned char * buf, const unsigned char * key,
		uint key_len, enum ha_rkey_function find_flag);
	UNIV_INTERN int index_read_idx(unsigned char * buf, uint index, const unsigned char * key,
			   uint key_len, enum ha_rkey_function find_flag);
	UNIV_INTERN int index_read_last(unsigned char * buf, const unsigned char * key, uint key_len);
	UNIV_INTERN int index_next(unsigned char * buf);
	UNIV_INTERN int index_next_same(unsigned char * buf, const unsigned char *key, uint keylen);
	UNIV_INTERN int index_prev(unsigned char * buf);
	UNIV_INTERN int index_first(unsigned char * buf);
	UNIV_INTERN int index_last(unsigned char * buf);

	UNIV_INTERN int rnd_init(bool scan);
	UNIV_INTERN int rnd_end();
	UNIV_INTERN int rnd_next(unsigned char *buf);
	UNIV_INTERN int rnd_pos(unsigned char * buf, unsigned char *pos);

	UNIV_INTERN void position(const unsigned char *record);
	UNIV_INTERN int info(uint);
	UNIV_INTERN int analyze(Session* session);
	UNIV_INTERN int discard_or_import_tablespace(bool discard);
	UNIV_INTERN int extra(enum ha_extra_function operation);
        UNIV_INTERN int reset();
	UNIV_INTERN int external_lock(Session *session, int lock_type);
	void position(unsigned char *record);
	UNIV_INTERN ha_rows records_in_range(uint inx, key_range *min_key, key_range
								*max_key);
	UNIV_INTERN ha_rows estimate_rows_upper_bound();

	UNIV_INTERN int delete_all_rows();
	UNIV_INTERN int check(Session* session);
	UNIV_INTERN char* update_table_comment(const char* comment);
	UNIV_INTERN char* get_foreign_key_create_info();
	UNIV_INTERN int get_foreign_key_list(Session *session, List<FOREIGN_KEY_INFO> *f_key_list);
	UNIV_INTERN bool can_switch_engines();
	UNIV_INTERN uint referenced_by_foreign_key();
	UNIV_INTERN void free_foreign_key_create_info(char* str);
	UNIV_INTERN THR_LOCK_DATA **store_lock(Session *session, THR_LOCK_DATA **to,
					enum thr_lock_type lock_type);
        UNIV_INTERN virtual void get_auto_increment(uint64_t offset, 
                                                    uint64_t increment,
                                                    uint64_t nb_desired_values,
                                                    uint64_t *first_value,
                                                    uint64_t *nb_reserved_values);
        UNIV_INTERN int reset_auto_increment(uint64_t value);

	UNIV_INTERN bool primary_key_is_clustered();
	UNIV_INTERN int cmp_ref(const unsigned char *ref1, const unsigned char *ref2);
	/** Fast index creation (smart ALTER TABLE) @see handler0alter.cc @{ */
	UNIV_INTERN int add_index(TABLE *table_arg, KEY *key_info, uint num_of_keys);
	UNIV_INTERN int prepare_drop_index(TABLE *table_arg, uint *key_num,
                                           uint num_of_keys);
        UNIV_INTERN int final_drop_index(TABLE *table_arg);
	/** @} */
public:
  int read_range_first(const key_range *start_key, const key_range *end_key,
		       bool eq_range_arg, bool sorted);
  int read_range_next();
};


extern "C" {
char **session_query(Session *session);

/** Get the file name of the MySQL binlog.
 * @return the name of the binlog file
 */
const char* drizzle_bin_log_file_name(void);

/** Get the current position of the MySQL binlog.
 * @return byte offset from the beginning of the binlog
 */
uint64_t drizzle_bin_log_file_pos(void);

/**
  Check if a user thread is a replication slave thread
  @param session  user thread
  @retval 0 the user thread is not a replication slave thread
  @retval 1 the user thread is a replication slave thread
*/
int session_slave_thread(const Session *session);

/**
  Check if a user thread is running a non-transactional update
  @param session  user thread
  @retval 0 the user thread is not running a non-transactional update
  @retval 1 the user thread is running a non-transactional update
*/
int session_non_transactional_update(const Session *session);

/**
  Get the user thread's binary logging format
  @param session  user thread
  @return Value to be used as index into the binlog_format_names array
*/
int session_binlog_format(const Session *session);

/**
  Mark transaction to rollback and mark error as fatal to a sub-statement.
  @param  session   Thread handle
  @param  all   TRUE <=> rollback main transaction.
*/
void session_mark_transaction_to_rollback(Session *session, bool all);
}

typedef struct trx_struct trx_t;
/********************************************************************//**
@file Cursor/ha_innodb.h
Converts an InnoDB error code to a MySQL error code and also tells to MySQL
about a possible transaction rollback inside InnoDB caused by a lock wait
timeout or a deadlock.
@return	MySQL error code */
extern "C" UNIV_INTERN
int
convert_error_code_to_mysql(
/*========================*/
	int		error,		/*!< in: InnoDB error code */
	ulint		flags,		/*!< in: InnoDB table flags, or 0 */
	Session		*session);	/*!< in: user thread handle or NULL */

/*********************************************************************//**
Allocates an InnoDB transaction for a MySQL Cursor object.
@return	InnoDB transaction handle */
extern "C" UNIV_INTERN
trx_t*
innobase_trx_allocate(
/*==================*/
	Session		*session);	/*!< in: user thread handle */
#endif /* INNODB_HANDLER_HA_INNODB_H */
