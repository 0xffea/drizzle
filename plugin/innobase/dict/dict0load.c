/*****************************************************************************

Copyright (c) 1996, 2010, Innobase Oy. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
St, Fifth Floor, Boston, MA 02110-1301 USA

*****************************************************************************/

/**************************************************//**
@file dict/dict0load.c
Loads to the memory cache database object definitions
from dictionary tables

Created 4/24/1996 Heikki Tuuri
*******************************************************/

#if defined(BUILD_DRIZZLE)
# include "config.h"
#else
# include "mysql_version.h"
#endif /* BUILD_DRIZZLE */
#include "dict0load.h"

#ifdef UNIV_NONINL
#include "dict0load.ic"
#endif

#include "btr0pcur.h"
#include "btr0btr.h"
#include "page0page.h"
#include "mach0data.h"
#include "dict0dict.h"
#include "dict0boot.h"
#include "rem0cmp.h"
#include "srv0start.h"
#include "srv0srv.h"


/** Following are six InnoDB system tables */
static const char* SYSTEM_TABLE_NAME[] = {
	"SYS_TABLES",
	"SYS_INDEXES",
	"SYS_COLUMNS",
	"SYS_FIELDS",
	"SYS_FOREIGN",
	"SYS_FOREIGN_COLS"
};
/****************************************************************//**
Compare the name of an index column.
@return	TRUE if the i'th column of index is 'name'. */
static
ibool
name_of_col_is(
/*===========*/
	const dict_table_t*	table,	/*!< in: table */
	const dict_index_t*	index,	/*!< in: index */
	ulint			i,	/*!< in: index field offset */
	const char*		name)	/*!< in: name to compare to */
{
	ulint	tmp = dict_col_get_no(dict_field_get_col(
					      dict_index_get_nth_field(
						      index, i)));

	return(strcmp(name, dict_table_get_col_name(table, tmp)) == 0);
}

/********************************************************************//**
Finds the first table name in the given database.
@return own: table name, NULL if does not exist; the caller must free
the memory in the string! */
UNIV_INTERN
char*
dict_get_first_table_name_in_db(
/*============================*/
	const char*	name)	/*!< in: database name which ends in '/' */
{
	dict_table_t*	sys_tables;
	btr_pcur_t	pcur;
	dict_index_t*	sys_index;
	dtuple_t*	tuple;
	mem_heap_t*	heap;
	dfield_t*	dfield;
	const rec_t*	rec;
	const byte*	field;
	ulint		len;
	mtr_t		mtr;

	ut_ad(mutex_own(&(dict_sys->mutex)));

	heap = mem_heap_create(1000);

	mtr_start(&mtr);

	sys_tables = dict_table_get_low("SYS_TABLES");
	sys_index = UT_LIST_GET_FIRST(sys_tables->indexes);
	ut_a(!dict_table_is_comp(sys_tables));

	tuple = dtuple_create(heap, 1);
	dfield = dtuple_get_nth_field(tuple, 0);

	dfield_set_data(dfield, name, ut_strlen(name));
	dict_index_copy_types(tuple, sys_index, 1);

	btr_pcur_open_on_user_rec(sys_index, tuple, PAGE_CUR_GE,
				  BTR_SEARCH_LEAF, &pcur, &mtr);
loop:
	rec = btr_pcur_get_rec(&pcur);

	if (!btr_pcur_is_on_user_rec(&pcur)) {
		/* Not found */

		btr_pcur_close(&pcur);
		mtr_commit(&mtr);
		mem_heap_free(heap);

		return(NULL);
	}

	field = rec_get_nth_field_old(rec, 0, &len);

	if (len < strlen(name)
	    || ut_memcmp(name, field, strlen(name)) != 0) {
		/* Not found */

		btr_pcur_close(&pcur);
		mtr_commit(&mtr);
		mem_heap_free(heap);

		return(NULL);
	}

	if (!rec_get_deleted_flag(rec, 0)) {

		/* We found one */

		char*	table_name = mem_strdupl((char*) field, len);

		btr_pcur_close(&pcur);
		mtr_commit(&mtr);
		mem_heap_free(heap);

		return(table_name);
	}

	btr_pcur_move_to_next_user_rec(&pcur, &mtr);

	goto loop;
}

/********************************************************************//**
Prints to the standard output information on all tables found in the data
dictionary system table. */
UNIV_INTERN
void
dict_print(void)
/*============*/
{
	dict_table_t*	table;
	btr_pcur_t	pcur;
	const rec_t*	rec;
	mem_heap_t*	heap;
	mtr_t		mtr;

	/* Enlarge the fatal semaphore wait timeout during the InnoDB table
	monitor printout */

	mutex_enter(&kernel_mutex);
	srv_fatal_semaphore_wait_threshold += 7200; /* 2 hours */
	mutex_exit(&kernel_mutex);

        heap = mem_heap_create(1000);
	mutex_enter(&(dict_sys->mutex));

	mtr_start(&mtr);

	rec = dict_startscan_system(&pcur, &mtr, SYS_TABLES);

	while (rec) {
		const char* err_msg;

		err_msg = dict_process_sys_tables_rec(
			heap, rec, &table, DICT_TABLE_LOAD_FROM_CACHE
			| DICT_TABLE_UPDATE_STATS);

		mtr_commit(&mtr);

		if (!err_msg) {
			dict_table_print_low(table);
		} else {
			ut_print_timestamp(stderr);
			fprintf(stderr, "  InnoDB: %s\n", err_msg);
		}

		mem_heap_empty(heap);

		mtr_start(&mtr);
		rec = dict_getnext_system(&pcur, &mtr);
	}

	mtr_commit(&mtr);
	mutex_exit(&(dict_sys->mutex));
	mem_heap_free(heap);

	/* Restore the fatal semaphore wait timeout */
	mutex_enter(&kernel_mutex);
	srv_fatal_semaphore_wait_threshold -= 7200; /* 2 hours */
	mutex_exit(&kernel_mutex);
}


/********************************************************************//**
This function gets the next system table record as it scans the table.
@return	the next record if found, NULL if end of scan */
static
const rec_t*
dict_getnext_system_low(
/*====================*/
	btr_pcur_t*	pcur,		/*!< in/out: persistent cursor to the
					record*/
	mtr_t*		mtr)		/*!< in: the mini-transaction */
{
	rec_t*	rec = NULL;

	while (!rec || rec_get_deleted_flag(rec, 0)) {
		btr_pcur_move_to_next_user_rec(pcur, mtr);

		rec = btr_pcur_get_rec(pcur);

		if (!btr_pcur_is_on_user_rec(pcur)) {
			/* end of index */
			btr_pcur_close(pcur);

			return(NULL);
		}
	}

	/* Get a record, let's save the position */
	btr_pcur_store_position(pcur, mtr);

	return(rec);
}

/********************************************************************//**
This function opens a system table, and return the first record.
@return	first record of the system table */
UNIV_INTERN
const rec_t*
dict_startscan_system(
/*==================*/
	btr_pcur_t*	pcur,		/*!< out: persistent cursor to
					the record */
	mtr_t*		mtr,		/*!< in: the mini-transaction */
	dict_system_id_t system_id)	/*!< in: which system table to open */
{
	dict_table_t*	system_table;
	dict_index_t*	clust_index;
	const rec_t*	rec;

	ut_a(system_id < SYS_NUM_SYSTEM_TABLES);

	system_table = dict_table_get_low(SYSTEM_TABLE_NAME[system_id]);

	clust_index = UT_LIST_GET_FIRST(system_table->indexes);

	btr_pcur_open_at_index_side(TRUE, clust_index, BTR_SEARCH_LEAF, pcur,
				    TRUE, mtr);

	rec = dict_getnext_system_low(pcur, mtr);

	return(rec);
}

/********************************************************************//**
This function gets the next system table record as it scans the table.
@return	the next record if found, NULL if end of scan */
UNIV_INTERN
const rec_t*
dict_getnext_system(
/*================*/
	btr_pcur_t*	pcur,		/*!< in/out: persistent cursor
					to the record */
	mtr_t*		mtr)		/*!< in: the mini-transaction */
{
        const rec_t*	rec;

	/* Restore the position */
        btr_pcur_restore_position(BTR_SEARCH_LEAF, pcur, mtr);

	/* Get the next record */
        rec = dict_getnext_system_low(pcur, mtr);

	return(rec);
}
/********************************************************************//**
This function processes one SYS_TABLES record and populate the dict_table_t
struct for the table. Extracted out of dict_print() to be used by
both monitor table output and information schema innodb_sys_tables output.
@return error message, or NULL on success */
UNIV_INTERN
const char*
dict_process_sys_tables_rec(
/*========================*/
	mem_heap_t*	heap,		/*!< in/out: temporary memory heap */
	const rec_t*	rec,		/*!< in: SYS_TABLES record */
	dict_table_t**	table,		/*!< out: dict_table_t to fill */
	dict_table_info_t status)	/*!< in: status bit controls
					options such as whether we shall
					look for dict_table_t from cache
					first */
{
	ulint		len;
	const byte*	field;
	const char*	err_msg = NULL;
	char*		table_name;

	field = rec_get_nth_field_old(rec, 0, &len);

	ut_a(!rec_get_deleted_flag(rec, 0));

	/* Get the table name */
	table_name = mem_heap_strdupl(heap, (const char*)field, len);

	/* If DICT_TABLE_LOAD_FROM_CACHE is set, first check
	whether there is cached dict_table_t struct first */
	if (status & DICT_TABLE_LOAD_FROM_CACHE) {
		*table = dict_table_get_low(table_name);

		if (!(*table)) {
			err_msg = "Table not found in cache";
		}
	} else {
		err_msg = dict_load_table_low(table_name, rec, table);
	}

	if (err_msg) {
		return(err_msg);
	}

	if ((status & DICT_TABLE_UPDATE_STATS)
	    && dict_table_get_first_index(*table)) {

		/* Update statistics if DICT_TABLE_UPDATE_STATS
		is set */
		dict_update_statistics_low(*table, TRUE);
	}

	return(NULL);
}

/********************************************************************//**
This function parses a SYS_INDEXES record and populate a dict_index_t
structure with the information from the record. For detail information
about SYS_INDEXES fields, please refer to dict_boot() function.
@return error message, or NULL on success */
UNIV_INTERN
const char*
dict_process_sys_indexes_rec(
/*=========================*/
	mem_heap_t*	heap,		/*!< in/out: heap memory */
	const rec_t*	rec,		/*!< in: current SYS_INDEXES rec */
	dict_index_t*	index,		/*!< out: index to be filled */
	dulint*		table_id)	/*!< out: index table id */
{
	const char*	err_msg;
	byte*		buf;

	buf = mem_heap_alloc(heap, 8);

	/* Parse the record, and get "dict_index_t" struct filled */
	err_msg = dict_load_index_low(buf, NULL,
				      heap, rec, FALSE, &index);

	*table_id = mach_read_from_8(buf);

	return(err_msg);
}
/********************************************************************//**
This function parses a SYS_COLUMNS record and populate a dict_column_t
structure with the information from the record.
@return error message, or NULL on success */
UNIV_INTERN
const char*
dict_process_sys_columns_rec(
/*=========================*/
	mem_heap_t*	heap,		/*!< in/out: heap memory */
	const rec_t*	rec,		/*!< in: current SYS_COLUMNS rec */
	dict_col_t*	column,		/*!< out: dict_col_t to be filled */
	dulint*		table_id,	/*!< out: table id */
	const char**	col_name)	/*!< out: column name */
{
	const char*	err_msg;

	/* Parse the record, and get "dict_col_t" struct filled */
	err_msg = dict_load_column_low(NULL, heap, column,
				       table_id, col_name, rec);

	return(err_msg);
}
/********************************************************************//**
This function parses a SYS_FIELDS record and populates a dict_field_t
structure with the information from the record.
@return error message, or NULL on success */
UNIV_INTERN
const char*
dict_process_sys_fields_rec(
/*========================*/
	mem_heap_t*	heap,		/*!< in/out: heap memory */
	const rec_t*	rec,		/*!< in: current SYS_FIELDS rec */
	dict_field_t*	sys_field,	/*!< out: dict_field_t to be
					filled */
	ulint*		pos,		/*!< out: Field position */
	dulint*		index_id,	/*!< out: current index id */
	dulint		last_id)	/*!< in: previous index id */
{
	byte*		buf;
	byte*		last_index_id;
	const char*	err_msg;

	buf = mem_heap_alloc(heap, 8);

	last_index_id = mem_heap_alloc(heap, 8);
	mach_write_to_8(last_index_id, last_id);

	err_msg = dict_load_field_low(buf, NULL, sys_field,
				      pos, last_index_id, heap, rec);

	*index_id = mach_read_from_8(buf);

	return(err_msg);

}
/********************************************************************//**
This function parses a SYS_FOREIGN record and populate a dict_foreign_t
structure with the information from the record. For detail information
about SYS_FOREIGN fields, please refer to dict_load_foreign() function
@return error message, or NULL on success */
UNIV_INTERN
const char*
dict_process_sys_foreign_rec(
/*=========================*/
	mem_heap_t*	heap,		/*!< in/out: heap memory */
	const rec_t*	rec,		/*!< in: current SYS_FOREIGN rec */
	dict_foreign_t*	foreign)	/*!< out: dict_foreign_t struct
					to be filled */
{
	ulint		len;
	const byte*	field;
	ulint		n_fields_and_type;

	if (UNIV_UNLIKELY(rec_get_deleted_flag(rec, 0))) {
		return("delete-marked record in SYS_FOREIGN");
	}

	if (UNIV_UNLIKELY(rec_get_n_fields_old(rec) != 6)) {
		return("wrong number of columns in SYS_FOREIGN record");
	}

	field = rec_get_nth_field_old(rec, 0/*ID*/, &len);
	if (UNIV_UNLIKELY(len < 1 || len == UNIV_SQL_NULL)) {
err_len:
		return("incorrect column length in SYS_FOREIGN");
	}
	foreign->id = mem_heap_strdupl(heap, (const char*) field, len);

	rec_get_nth_field_offs_old(rec, 1/*DB_TRX_ID*/, &len);
	if (UNIV_UNLIKELY(len != DATA_TRX_ID_LEN && len != UNIV_SQL_NULL)) {
		goto err_len;
	}
	rec_get_nth_field_offs_old(rec, 2/*DB_ROLL_PTR*/, &len);
	if (UNIV_UNLIKELY(len != DATA_ROLL_PTR_LEN && len != UNIV_SQL_NULL)) {
		goto err_len;
	}

	field = rec_get_nth_field_old(rec, 3/*FOR_NAME*/, &len);
	if (UNIV_UNLIKELY(len < 1 || len == UNIV_SQL_NULL)) {
		goto err_len;
	}
	foreign->foreign_table_name = mem_heap_strdupl(
		heap, (const char*) field, len);

	field = rec_get_nth_field_old(rec, 4/*REF_NAME*/, &len);
	if (UNIV_UNLIKELY(len < 1 || len == UNIV_SQL_NULL)) {
		goto err_len;
	}
	foreign->referenced_table_name = mem_heap_strdupl(
		heap, (const char*) field, len);

	field = rec_get_nth_field_old(rec, 5/*N_COLS*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}
	n_fields_and_type = mach_read_from_4(field);

	foreign->type = (unsigned int) (n_fields_and_type >> 24);
	foreign->n_fields = (unsigned int) (n_fields_and_type & 0x3FFUL);

	return(NULL);
}
/********************************************************************//**
This function parses a SYS_FOREIGN_COLS record and extract necessary
information from the record and return to caller.
@return error message, or NULL on success */
UNIV_INTERN
const char*
dict_process_sys_foreign_col_rec(
/*=============================*/
	mem_heap_t*	heap,		/*!< in/out: heap memory */
	const rec_t*	rec,		/*!< in: current SYS_FOREIGN_COLS rec */
	const char**	name,		/*!< out: foreign key constraint name */
	const char**	for_col_name,	/*!< out: referencing column name */
	const char**	ref_col_name,	/*!< out: referenced column name
					in referenced table */
	ulint*		pos)		/*!< out: column position */
{
	ulint		len;
	const byte*	field;

	if (UNIV_UNLIKELY(rec_get_deleted_flag(rec, 0))) {
		return("delete-marked record in SYS_FOREIGN_COLS");
	}

	if (UNIV_UNLIKELY(rec_get_n_fields_old(rec) != 6)) {
		return("wrong number of columns in SYS_FOREIGN_COLS record");
	}

	field = rec_get_nth_field_old(rec, 0/*ID*/, &len);
	if (UNIV_UNLIKELY(len < 1 || len == UNIV_SQL_NULL)) {
err_len:
		return("incorrect column length in SYS_FOREIGN_COLS");
	}
	*name = mem_heap_strdupl(heap, (char*) field, len);

	field = rec_get_nth_field_old(rec, 1/*POS*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}
	*pos = mach_read_from_4(field);

	rec_get_nth_field_offs_old(rec, 2/*DB_TRX_ID*/, &len);
	if (UNIV_UNLIKELY(len != DATA_TRX_ID_LEN && len != UNIV_SQL_NULL)) {
		goto err_len;
	}
	rec_get_nth_field_offs_old(rec, 3/*DB_ROLL_PTR*/, &len);
	if (UNIV_UNLIKELY(len != DATA_ROLL_PTR_LEN && len != UNIV_SQL_NULL)) {
		goto err_len;
	}

	field = rec_get_nth_field_old(rec, 4/*FOR_COL_NAME*/, &len);
	if (UNIV_UNLIKELY(len < 1 || len == UNIV_SQL_NULL)) {
		goto err_len;
	}
	*for_col_name = mem_heap_strdupl(heap, (char*) field, len);

	field = rec_get_nth_field_old(rec, 5/*REF_COL_NAME*/, &len);
	if (UNIV_UNLIKELY(len < 1 || len == UNIV_SQL_NULL)) {
		goto err_len;
	}
	*ref_col_name = mem_heap_strdupl(heap, (char*) field, len);

	return(NULL);
}

/*
  Send data to callback function .
*/

UNIV_INTERN void dict_print_with_callback(dict_print_callback func, void *func_arg)
{
	dict_table_t*	sys_tables;
	dict_index_t*	sys_index;
	btr_pcur_t	pcur;
	const rec_t*	rec;
	const byte*	field;
	ulint		len;
	mtr_t		mtr;

	/* Enlarge the fatal semaphore wait timeout during the InnoDB table
	monitor printout */

	mutex_enter(&kernel_mutex);
	srv_fatal_semaphore_wait_threshold += 7200; /* 2 hours */
	mutex_exit(&kernel_mutex);

	mutex_enter(&(dict_sys->mutex));

	mtr_start(&mtr);

	sys_tables = dict_table_get_low("SYS_TABLES");
	sys_index = UT_LIST_GET_FIRST(sys_tables->indexes);

	btr_pcur_open_at_index_side(TRUE, sys_index, BTR_SEARCH_LEAF, &pcur,
				    TRUE, &mtr);
loop:
	btr_pcur_move_to_next_user_rec(&pcur, &mtr);

	rec = btr_pcur_get_rec(&pcur);

	if (!btr_pcur_is_on_user_rec(&pcur)) {
		/* end of index */

		btr_pcur_close(&pcur);
		mtr_commit(&mtr);

		mutex_exit(&(dict_sys->mutex));

		/* Restore the fatal semaphore wait timeout */

		mutex_enter(&kernel_mutex);
		srv_fatal_semaphore_wait_threshold -= 7200; /* 2 hours */
		mutex_exit(&kernel_mutex);

		return;
	}

	field = rec_get_nth_field_old(rec, 0, &len);

	if (!rec_get_deleted_flag(rec, 0)) {

		/* We found one */

		char*	table_name = mem_strdupl((char*) field, len);

		btr_pcur_store_position(&pcur, &mtr);

		mtr_commit(&mtr);

                func(func_arg, table_name);
		mem_free(table_name);

		mtr_start(&mtr);

		btr_pcur_restore_position(BTR_SEARCH_LEAF, &pcur, &mtr);
	}

	goto loop;
}

/********************************************************************//**
Determine the flags of a table described in SYS_TABLES.
@return compressed page size in kilobytes; or 0 if the tablespace is
uncompressed, ULINT_UNDEFINED on error */
static
ulint
dict_sys_tables_get_flags(
/*======================*/
	const rec_t*	rec)	/*!< in: a record of SYS_TABLES */
{
	const byte*	field;
	ulint		len;
	ulint		n_cols;
	ulint		flags;

	field = rec_get_nth_field_old(rec, 5, &len);
	ut_a(len == 4);

	flags = mach_read_from_4(field);

	if (UNIV_LIKELY(flags == DICT_TABLE_ORDINARY)) {
		return(0);
	}

	field = rec_get_nth_field_old(rec, 4/*N_COLS*/, &len);
	n_cols = mach_read_from_4(field);

	if (UNIV_UNLIKELY(!(n_cols & 0x80000000UL))) {
		/* New file formats require ROW_FORMAT=COMPACT. */
		return(ULINT_UNDEFINED);
	}

	switch (flags & (DICT_TF_FORMAT_MASK | DICT_TF_COMPACT)) {
	default:
	case DICT_TF_FORMAT_51 << DICT_TF_FORMAT_SHIFT:
	case DICT_TF_FORMAT_51 << DICT_TF_FORMAT_SHIFT | DICT_TF_COMPACT:
		/* flags should be DICT_TABLE_ORDINARY,
		or DICT_TF_FORMAT_MASK should be nonzero. */
		return(ULINT_UNDEFINED);

	case DICT_TF_FORMAT_ZIP << DICT_TF_FORMAT_SHIFT | DICT_TF_COMPACT:
#if DICT_TF_FORMAT_MAX > DICT_TF_FORMAT_ZIP
# error "missing case labels for DICT_TF_FORMAT_ZIP .. DICT_TF_FORMAT_MAX"
#endif
		/* We support this format. */
		break;
	}

	if (UNIV_UNLIKELY((flags & DICT_TF_ZSSIZE_MASK)
			  > (DICT_TF_ZSSIZE_MAX << DICT_TF_ZSSIZE_SHIFT))) {
		/* Unsupported compressed page size. */
		return(ULINT_UNDEFINED);
	}

	if (UNIV_UNLIKELY(flags & (~0 << DICT_TF_BITS))) {
		/* Some unused bits are set. */
		return(ULINT_UNDEFINED);
	}

	return(flags);
}

/********************************************************************//**
In a crash recovery we already have all the tablespace objects created.
This function compares the space id information in the InnoDB data dictionary
to what we already read with fil_load_single_table_tablespaces().

In a normal startup, we create the tablespace objects for every table in
InnoDB's data dictionary, if the corresponding .ibd file exists.
We also scan the biggest space id, and store it to fil_system. */
UNIV_INTERN
void
dict_check_tablespaces_and_store_max_id(
/*====================================*/
	ibool	in_crash_recovery)	/*!< in: are we doing a crash recovery */
{
	dict_table_t*	sys_tables;
	dict_index_t*	sys_index;
	btr_pcur_t	pcur;
	const rec_t*	rec;
	ulint		max_space_id	= 0;
	mtr_t		mtr;

	mutex_enter(&(dict_sys->mutex));

	mtr_start(&mtr);

	sys_tables = dict_table_get_low("SYS_TABLES");
	sys_index = UT_LIST_GET_FIRST(sys_tables->indexes);
	ut_a(!dict_table_is_comp(sys_tables));

	btr_pcur_open_at_index_side(TRUE, sys_index, BTR_SEARCH_LEAF, &pcur,
				    TRUE, &mtr);
loop:
	btr_pcur_move_to_next_user_rec(&pcur, &mtr);

	rec = btr_pcur_get_rec(&pcur);

	if (!btr_pcur_is_on_user_rec(&pcur)) {
		/* end of index */

		btr_pcur_close(&pcur);
		mtr_commit(&mtr);

		/* We must make the tablespace cache aware of the biggest
		known space id */

		/* printf("Biggest space id in data dictionary %lu\n",
		max_space_id); */
		fil_set_max_space_id_if_bigger(max_space_id);

		mutex_exit(&(dict_sys->mutex));

		return;
	}

	if (!rec_get_deleted_flag(rec, 0)) {

		/* We found one */
		const byte*	field;
		ulint		len;
		ulint		space_id;
		ulint		flags;
		char*		name;

		field = rec_get_nth_field_old(rec, 0, &len);
		name = mem_strdupl((char*) field, len);

		flags = dict_sys_tables_get_flags(rec);
		if (UNIV_UNLIKELY(flags == ULINT_UNDEFINED)) {

			field = rec_get_nth_field_old(rec, 5, &len);
			flags = mach_read_from_4(field);

			ut_print_timestamp(stderr);
			fputs("  InnoDB: Error: table ", stderr);
			ut_print_filename(stderr, name);
			fprintf(stderr, "\n"
				"InnoDB: in InnoDB data dictionary"
				" has unknown type %lx.\n",
				(ulong) flags);

			goto loop;
		}

		field = rec_get_nth_field_old(rec, 9, &len);
		ut_a(len == 4);

		space_id = mach_read_from_4(field);

		btr_pcur_store_position(&pcur, &mtr);

		mtr_commit(&mtr);

		if (space_id == 0) {
			/* The system tablespace always exists. */
		} else if (in_crash_recovery) {
			/* Check that the tablespace (the .ibd file) really
			exists; print a warning to the .err log if not.
			Do not print warnings for temporary tables. */
			ibool	is_temp;

			field = rec_get_nth_field_old(rec, 4, &len);
			if (0x80000000UL &  mach_read_from_4(field)) {
				/* ROW_FORMAT=COMPACT: read the is_temp
				flag from SYS_TABLES.MIX_LEN. */
				field = rec_get_nth_field_old(rec, 7, &len);
				is_temp = mach_read_from_4(field)
					& DICT_TF2_TEMPORARY;
			} else {
				/* For tables created with old versions
				of InnoDB, SYS_TABLES.MIX_LEN may contain
				garbage.  Such tables would always be
				in ROW_FORMAT=REDUNDANT.  Pretend that
				all such tables are non-temporary.  That is,
				do not suppress error printouts about
				temporary tables not being found. */
				is_temp = FALSE;
			}

			fil_space_for_table_exists_in_mem(
				space_id, name, is_temp, TRUE, !is_temp);
		} else {
			/* It is a normal database startup: create the space
			object and check that the .ibd file exists. */

			fil_open_single_table_tablespace(FALSE, space_id,
							 flags, name);
		}

		mem_free(name);

		if (space_id > max_space_id) {
			max_space_id = space_id;
		}

		mtr_start(&mtr);

		btr_pcur_restore_position(BTR_SEARCH_LEAF, &pcur, &mtr);
	}

	goto loop;
}

/********************************************************************//**
Loads a table column definition from a SYS_COLUMNS record to
dict_table_t.
@return error message, or NULL on success */
UNIV_INTERN
const char*
dict_load_column_low(
/*=================*/
	dict_table_t*	table,		/*!< in/out: table, could be NULL
					if we just polulate a dict_column_t
					struct with information from
					a SYS_COLUMNS record */
	mem_heap_t*	heap,		/*!< in/out: memory heap
					for temporary storage */
	dict_col_t*	column,		/*!< out: dict_column_t to fill */
	dulint*		table_id,	/*!< out: table id */
	const char**	col_name,	/*!< out: column name */
	const rec_t*	rec)		/*!< in: SYS_COLUMNS record */
{
	char*		name;
	const byte*	field;
	ulint		len;
	ulint		mtype;
	ulint		prtype;
	ulint		col_len;
	ulint		pos;

	if (UNIV_UNLIKELY(rec_get_deleted_flag(rec, 0))) {
		return("delete-marked record in SYS_COLUMNS");
	}

	if (UNIV_UNLIKELY(rec_get_n_fields_old(rec) != 9)) {
		return("wrong number of columns in SYS_COLUMNS record");
	}

	field = rec_get_nth_field_old(rec, 0/*TABLE_ID*/, &len);
	if (UNIV_UNLIKELY(len != 8)) {
err_len:
		return("incorrect column length in SYS_COLUMNS");
	}

	if (table_id) {
		*table_id = mach_read_from_8(field);
	} else if (UNIV_UNLIKELY(ut_dulint_cmp(table->id,
				 mach_read_from_8(field)))) {
		return("SYS_COLUMNS.TABLE_ID mismatch");
	}

	field = rec_get_nth_field_old(rec, 1/*POS*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {

		goto err_len;
	}

	if (!table) {
		pos = mach_read_from_4(field);
	} else if (UNIV_UNLIKELY(table->n_def != mach_read_from_4(field))) {
		return("SYS_COLUMNS.POS mismatch");
	}

	rec_get_nth_field_offs_old(rec, 2/*DB_TRX_ID*/, &len);
	if (UNIV_UNLIKELY(len != DATA_TRX_ID_LEN && len != UNIV_SQL_NULL)) {
		goto err_len;
	}
	rec_get_nth_field_offs_old(rec, 3/*DB_ROLL_PTR*/, &len);
	if (UNIV_UNLIKELY(len != DATA_ROLL_PTR_LEN && len != UNIV_SQL_NULL)) {
		goto err_len;
	}

	field = rec_get_nth_field_old(rec, 4/*NAME*/, &len);
	if (UNIV_UNLIKELY(len < 1 || len == UNIV_SQL_NULL)) {
		goto err_len;
	}

	name = mem_heap_strdupl(heap, (const char*) field, len);

	if (col_name) {
		*col_name = name;
	}

	field = rec_get_nth_field_old(rec, 5/*MTYPE*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}

	mtype = mach_read_from_4(field);

	field = rec_get_nth_field_old(rec, 6/*PRTYPE*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}
	prtype = mach_read_from_4(field);

	if (dtype_get_charset_coll(prtype) == 0
	    && dtype_is_string_type(mtype)) {
		/* The table was created with < 4.1.2. */

		if (dtype_is_binary_string_type(mtype, prtype)) {
			/* Use the binary collation for
			string columns of binary type. */

			prtype = dtype_form_prtype(
				prtype,
				DATA_MYSQL_BINARY_CHARSET_COLL);
		} else {
			/* Use the default charset for
			other than binary columns. */

			prtype = dtype_form_prtype(
				prtype,
				data_mysql_default_charset_coll);
		}
	}

	field = rec_get_nth_field_old(rec, 7/*LEN*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}
	col_len = mach_read_from_4(field);
	field = rec_get_nth_field_old(rec, 8/*PREC*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}

	if (!column) {
		dict_mem_table_add_col(table, heap, name, mtype,
				       prtype, col_len);
	} else {
		dict_mem_fill_column_struct(column, pos, mtype,
					    prtype, col_len);
	}

	return(NULL);
}

/********************************************************************//**
Loads definitions for table columns. */
static
void
dict_load_columns(
/*==============*/
	dict_table_t*	table,	/*!< in/out: table */
	mem_heap_t*	heap)	/*!< in/out: memory heap
				for temporary storage */
{
	dict_table_t*	sys_columns;
	dict_index_t*	sys_index;
	btr_pcur_t	pcur;
	dtuple_t*	tuple;
	dfield_t*	dfield;
	const rec_t*	rec;
	byte*		buf;
	ulint		i;
	mtr_t		mtr;

	ut_ad(mutex_own(&(dict_sys->mutex)));

	mtr_start(&mtr);

	sys_columns = dict_table_get_low("SYS_COLUMNS");
	sys_index = UT_LIST_GET_FIRST(sys_columns->indexes);
	ut_a(!dict_table_is_comp(sys_columns));

	ut_a(name_of_col_is(sys_columns, sys_index, 4, "NAME"));
	ut_a(name_of_col_is(sys_columns, sys_index, 8, "PREC"));

	tuple = dtuple_create(heap, 1);
	dfield = dtuple_get_nth_field(tuple, 0);

	buf = mem_heap_alloc(heap, 8);
	mach_write_to_8(buf, table->id);

	dfield_set_data(dfield, buf, 8);
	dict_index_copy_types(tuple, sys_index, 1);

	btr_pcur_open_on_user_rec(sys_index, tuple, PAGE_CUR_GE,
				  BTR_SEARCH_LEAF, &pcur, &mtr);
	for (i = 0; i + DATA_N_SYS_COLS < (ulint) table->n_cols; i++) {
		const char* err_msg;

		rec = btr_pcur_get_rec(&pcur);

		ut_a(btr_pcur_is_on_user_rec(&pcur));

		err_msg = dict_load_column_low(table, heap, NULL, NULL,
					       NULL, rec);

		if (err_msg) {
			fprintf(stderr, "InnoDB: %s\n", err_msg);
			ut_error;
		}

		btr_pcur_move_to_next_user_rec(&pcur, &mtr);
	}

	btr_pcur_close(&pcur);
	mtr_commit(&mtr);
}

/** Error message for a delete-marked record in dict_load_field_low() */
static const char* dict_load_field_del = "delete-marked record in SYS_FIELDS";

/********************************************************************//**
Loads an index field definition from a SYS_FIELDS record to
dict_index_t.
@return error message, or NULL on success */
UNIV_INTERN
const char*
dict_load_field_low(
/*================*/
	byte*		index_id,	/*!< in/out: index id (8 bytes)
					an "in" value if index != NULL
                                        and "out" if index == NULL */
	dict_index_t*	index,		/*!< in/out: index, could be NULL
					if we just populate a dict_field_t
					struct with information from
					a SYS_FIELDSS record */
	dict_field_t*	sys_field,	/*!< out: dict_field_t to be
					filled */
	ulint*		pos,		/*!< out: Field position */
	byte*		last_index_id,	/*!< in: last index id */
	mem_heap_t*	heap,		/*!< in/out: memory heap
					for temporary storage */
	const rec_t*	rec)		/*!< in: SYS_FIELDS record */
{
	const byte*	field;
	ulint		len;
	ulint		pos_and_prefix_len;
	ulint		prefix_len;
	ibool		first_field;
	ulint		position;

	/* Either index or sys_field is supplied, not both */
	ut_a((!index) || (!sys_field));

	if (UNIV_UNLIKELY(rec_get_deleted_flag(rec, 0))) {
		return(dict_load_field_del);
	}

	if (UNIV_UNLIKELY(rec_get_n_fields_old(rec) != 5)) {
		return("wrong number of columns in SYS_FIELDS record");
	}

	field = rec_get_nth_field_old(rec, 0/*INDEX_ID*/, &len);
	if (UNIV_UNLIKELY(len != 8)) {
err_len:
		return("incorrect column length in SYS_FIELDS");
	}

	if (!index) {
		ut_a(last_index_id);
		memcpy(index_id, (const char*)field, 8);
		first_field = memcmp(index_id, last_index_id, 8);
	} else {
		first_field = (index->n_def == 0);
		if (memcmp(field, index_id, 8)) {
			return("SYS_FIELDS.INDEX_ID mismatch");
		}
	}

	field = rec_get_nth_field_old(rec, 1/*POS*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}

	rec_get_nth_field_offs_old(rec, 2/*DB_TRX_ID*/, &len);
	if (UNIV_UNLIKELY(len != DATA_TRX_ID_LEN && len != UNIV_SQL_NULL)) {
		goto err_len;
	}
	rec_get_nth_field_offs_old(rec, 3/*DB_ROLL_PTR*/, &len);
	if (UNIV_UNLIKELY(len != DATA_ROLL_PTR_LEN && len != UNIV_SQL_NULL)) {
		goto err_len;
	}

	/* The next field stores the field position in the index and a
	possible column prefix length if the index field does not
	contain the whole column. The storage format is like this: if
	there is at least one prefix field in the index, then the HIGH
	2 bytes contain the field number (index->n_def) and the low 2
	bytes the prefix length for the field. Otherwise the field
	number (index->n_def) is contained in the 2 LOW bytes. */

	pos_and_prefix_len = mach_read_from_4(field);

	if (index && UNIV_UNLIKELY
	    ((pos_and_prefix_len & 0xFFFFUL) != index->n_def
	     && (pos_and_prefix_len >> 16 & 0xFFFF) != index->n_def)) {
		return("SYS_FIELDS.POS mismatch");
	}

	if (first_field || pos_and_prefix_len > 0xFFFFUL) {
		prefix_len = pos_and_prefix_len & 0xFFFFUL;
		position = (pos_and_prefix_len & 0xFFFF0000UL)  >> 16;
	} else {
		prefix_len = 0;
		position = pos_and_prefix_len & 0xFFFFUL;
	}

	field = rec_get_nth_field_old(rec, 4, &len);
	if (UNIV_UNLIKELY(len < 1 || len == UNIV_SQL_NULL)) {
		goto err_len;
	}

	if (index) {
		dict_mem_index_add_field(
			index, mem_heap_strdupl(heap, (const char*) field, len),
			prefix_len);
	} else {
		ut_a(sys_field);
		ut_a(pos);

		sys_field->name = mem_heap_strdupl(
			heap, (const char*) field, len);
		sys_field->prefix_len = prefix_len;
		*pos = position;
	}

	return(NULL);
}

/********************************************************************//**
Loads definitions for index fields.
@return DB_SUCCESS if ok, DB_CORRUPTION if corruption */
static
ulint
dict_load_fields(
/*=============*/
	dict_index_t*	index,	/*!< in/out: index whose fields to load */
	mem_heap_t*	heap)	/*!< in: memory heap for temporary storage */
{
	dict_table_t*	sys_fields;
	dict_index_t*	sys_index;
	btr_pcur_t	pcur;
	dtuple_t*	tuple;
	dfield_t*	dfield;
	const rec_t*	rec;
	byte*		buf;
	ulint		i;
	mtr_t		mtr;
	ulint		error;

	ut_ad(mutex_own(&(dict_sys->mutex)));

	mtr_start(&mtr);

	sys_fields = dict_table_get_low("SYS_FIELDS");
	sys_index = UT_LIST_GET_FIRST(sys_fields->indexes);
	ut_a(!dict_table_is_comp(sys_fields));
	ut_a(name_of_col_is(sys_fields, sys_index, 4, "COL_NAME"));

	tuple = dtuple_create(heap, 1);
	dfield = dtuple_get_nth_field(tuple, 0);

	buf = mem_heap_alloc(heap, 8);
	mach_write_to_8(buf, index->id);

	dfield_set_data(dfield, buf, 8);
	dict_index_copy_types(tuple, sys_index, 1);

	btr_pcur_open_on_user_rec(sys_index, tuple, PAGE_CUR_GE,
				  BTR_SEARCH_LEAF, &pcur, &mtr);
	for (i = 0; i < index->n_fields; i++) {
		const char* err_msg;

		rec = btr_pcur_get_rec(&pcur);

		ut_a(btr_pcur_is_on_user_rec(&pcur));

		err_msg = dict_load_field_low(buf, index, NULL, NULL, NULL,
					      heap, rec);

		if (err_msg == dict_load_field_del) {
			/* There could be delete marked records in
			SYS_FIELDS because SYS_FIELDS.INDEX_ID can be
			updated by ALTER TABLE ADD INDEX. */

			goto next_rec;
		} else if (err_msg) {
			fprintf(stderr, "InnoDB: %s\n", err_msg);
			error = DB_CORRUPTION;
			goto func_exit;
		}
next_rec:
		btr_pcur_move_to_next_user_rec(&pcur, &mtr);
	}

	error = DB_SUCCESS;
func_exit:
	btr_pcur_close(&pcur);
	mtr_commit(&mtr);
	return(error);
}

/** Error message for a delete-marked record in dict_load_index_low() */
static const char* dict_load_index_del = "delete-marked record in SYS_INDEXES";
/** Error message for table->id mismatch in dict_load_index_low() */
static const char* dict_load_index_id_err = "SYS_INDEXES.TABLE_ID mismatch";

/********************************************************************//**
Loads an index definition from a SYS_INDEXES record to dict_index_t.
@return error message, or NULL on success */
UNIV_INTERN
const char*
dict_load_index_low(
/*================*/
	byte*		table_id,	/*!< in/out: table id (8 bytes),
					an "in" value if cached=TRUE
					and "out" when cached=FALSE */
	const char*	table_name,	/*!< in: table name */
	mem_heap_t*	heap,		/*!< in/out: temporary memory heap */
	const rec_t*	rec,		/*!< in: SYS_INDEXES record */
	ibool		cached,		/*!< in: TRUE = add to cache,
					FALSE = do not */
	dict_index_t**	index)		/*!< out,own: index, or NULL */
{
	const byte*	field;
	ulint		len;
	ulint		name_len;
	char*		name_buf;
	dulint		id;
	ulint		n_fields;
	ulint		type;
	ulint		space;

	if (UNIV_UNLIKELY(rec_get_deleted_flag(rec, 0))) {
		return(dict_load_index_del);
	}

	if (UNIV_UNLIKELY(rec_get_n_fields_old(rec) != 9)) {
		return("wrong number of columns in SYS_INDEXES record");
	}

	field = rec_get_nth_field_old(rec, 0/*TABLE_ID*/, &len);
	if (UNIV_UNLIKELY(len != 8)) {
err_len:
		return("incorrect column length in SYS_INDEXES");
	}

	if (!cached) {
		/* We are reading a SYS_INDEXES record. Copy the table_id */
		memcpy(table_id, (const char*)field, 8);
	} else if (memcmp(field, table_id, 8)) {
		/* Caller supplied table_id, verify it is the same
		id as on the index record */
		return(dict_load_index_id_err);
	}

	field = rec_get_nth_field_old(rec, 1/*ID*/, &len);
	if (UNIV_UNLIKELY(len != 8)) {
		goto err_len;
	}

	id = mach_read_from_8(field);

	rec_get_nth_field_offs_old(rec, 2/*DB_TRX_ID*/, &len);
	if (UNIV_UNLIKELY(len != DATA_TRX_ID_LEN && len != UNIV_SQL_NULL)) {
		goto err_len;
	}
	rec_get_nth_field_offs_old(rec, 3/*DB_ROLL_PTR*/, &len);
	if (UNIV_UNLIKELY(len != DATA_ROLL_PTR_LEN && len != UNIV_SQL_NULL)) {
		goto err_len;
	}

	field = rec_get_nth_field_old(rec, 4/*NAME*/, &name_len);
	if (UNIV_UNLIKELY(name_len == UNIV_SQL_NULL)) {
		goto err_len;
	}

	name_buf = mem_heap_strdupl(heap, (const char*) field,
				    name_len);

	field = rec_get_nth_field_old(rec, 5/*N_FIELDS*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}
	n_fields = mach_read_from_4(field);

	field = rec_get_nth_field_old(rec, 6/*TYPE*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}
	type = mach_read_from_4(field);

	field = rec_get_nth_field_old(rec, 7/*SPACE*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}
	space = mach_read_from_4(field);

	field = rec_get_nth_field_old(rec, 8/*PAGE_NO*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}

	if (cached) {
		*index = dict_mem_index_create(table_name, name_buf,
					       space, type, n_fields);
	} else {
		ut_a(*index);

		dict_mem_fill_index_struct(*index, NULL, NULL, name_buf,
					   space, type, n_fields);
	}

	(*index)->id = id;
	(*index)->page = mach_read_from_4(field);
	ut_ad((*index)->page);

	return(NULL);
}

/********************************************************************//**
Loads definitions for table indexes. Adds them to the data dictionary
cache.
@return DB_SUCCESS if ok, DB_CORRUPTION if corruption of dictionary
table or DB_UNSUPPORTED if table has unknown index type */
static
ulint
dict_load_indexes(
/*==============*/
	dict_table_t*	table,	/*!< in/out: table */
	mem_heap_t*	heap)	/*!< in: memory heap for temporary storage */
{
	dict_table_t*	sys_indexes;
	dict_index_t*	sys_index;
	btr_pcur_t	pcur;
	dtuple_t*	tuple;
	dfield_t*	dfield;
	const rec_t*	rec;
	byte*		buf;
	ibool		is_sys_table;
	mtr_t		mtr;
	ulint		error = DB_SUCCESS;

	ut_ad(mutex_own(&(dict_sys->mutex)));

	if ((ut_dulint_get_high(table->id) == 0)
	    && (ut_dulint_get_low(table->id) < DICT_HDR_FIRST_ID)) {
		is_sys_table = TRUE;
	} else {
		is_sys_table = FALSE;
	}

	mtr_start(&mtr);

	sys_indexes = dict_table_get_low("SYS_INDEXES");
	sys_index = UT_LIST_GET_FIRST(sys_indexes->indexes);
	ut_a(!dict_table_is_comp(sys_indexes));
	ut_a(name_of_col_is(sys_indexes, sys_index, 4, "NAME"));
	ut_a(name_of_col_is(sys_indexes, sys_index, 8, "PAGE_NO"));

	tuple = dtuple_create(heap, 1);
	dfield = dtuple_get_nth_field(tuple, 0);

	buf = mem_heap_alloc(heap, 8);
	mach_write_to_8(buf, table->id);

	dfield_set_data(dfield, buf, 8);
	dict_index_copy_types(tuple, sys_index, 1);

	btr_pcur_open_on_user_rec(sys_index, tuple, PAGE_CUR_GE,
				  BTR_SEARCH_LEAF, &pcur, &mtr);
	for (;;) {
		dict_index_t*	index;
		const char*	err_msg;

		if (!btr_pcur_is_on_user_rec(&pcur)) {

			break;
		}

		rec = btr_pcur_get_rec(&pcur);

		err_msg = dict_load_index_low(buf, table->name, heap, rec,
					      TRUE, &index);
		ut_ad((index == NULL) == (err_msg != NULL));

		if (err_msg == dict_load_index_id_err) {
			/* TABLE_ID mismatch means that we have
			run out of index definitions for the table. */
			break;
		} else if (err_msg == dict_load_index_del) {
			/* Skip delete-marked records. */
			goto next_rec;
		} else if (err_msg) {
			fprintf(stderr, "InnoDB: %s\n", err_msg);
			error = DB_CORRUPTION;
			goto func_exit;
		}

		ut_ad(index);

		/* We check for unsupported types first, so that the
		subsequent checks are relevant for the supported types. */
		if (index->type & ~(DICT_CLUSTERED | DICT_UNIQUE)) {

			fprintf(stderr,
				"InnoDB: Error: unknown type %lu"
				" of index %s of table %s\n",
				(ulong) index->type, index->name, table->name);

			error = DB_UNSUPPORTED;
			dict_mem_index_free(index);
			goto func_exit;
		} else if (index->page == FIL_NULL) {

			fprintf(stderr,
				"InnoDB: Error: trying to load index %s"
				" for table %s\n"
				"InnoDB: but the index tree has been freed!\n",
				index->name, table->name);

corrupted:
			dict_mem_index_free(index);
			error = DB_CORRUPTION;
			goto func_exit;
		} else if (!dict_index_is_clust(index)
			   && NULL == dict_table_get_first_index(table)) {

			fputs("InnoDB: Error: trying to load index ",
			      stderr);
			ut_print_name(stderr, NULL, FALSE, index->name);
			fputs(" for table ", stderr);
			ut_print_name(stderr, NULL, TRUE, table->name);
			fputs("\nInnoDB: but the first index"
			      " is not clustered!\n", stderr);

			goto corrupted;
		} else if (is_sys_table
			   && (dict_index_is_clust(index)
			       || ((table == dict_sys->sys_tables)
				   && !strcmp("ID_IND", index->name)))) {

			/* The index was created in memory already at booting
			of the database server */
			dict_mem_index_free(index);
		} else {
			dict_load_fields(index, heap);
			error = dict_index_add_to_cache(table, index,
							index->page, FALSE);
			/* The data dictionary tables should never contain
			invalid index definitions.  If we ignored this error
			and simply did not load this index definition, the
			.frm file would disagree with the index definitions
			inside InnoDB. */
			if (UNIV_UNLIKELY(error != DB_SUCCESS)) {

				goto func_exit;
			}
		}

next_rec:
		btr_pcur_move_to_next_user_rec(&pcur, &mtr);
	}

func_exit:
	btr_pcur_close(&pcur);
	mtr_commit(&mtr);

	return(error);
}

/********************************************************************//**
Loads a table definition from a SYS_TABLES record to dict_table_t.
Does not load any columns or indexes.
@return error message, or NULL on success */
UNIV_INTERN
const char*
dict_load_table_low(
/*================*/
	const char*	name,		/*!< in: table name */
	const rec_t*	rec,		/*!< in: SYS_TABLES record */
	dict_table_t**	table)		/*!< out,own: table, or NULL */
{
	const byte*	field;
	ulint		len;
	ulint		space;
	ulint		n_cols;
	ulint		flags;

	if (UNIV_UNLIKELY(rec_get_deleted_flag(rec, 0))) {
		return("delete-marked record in SYS_TABLES");
	}

	if (UNIV_UNLIKELY(rec_get_n_fields_old(rec) != 10)) {
		return("wrong number of columns in SYS_TABLES record");
	}

	rec_get_nth_field_offs_old(rec, 0/*NAME*/, &len);
	if (UNIV_UNLIKELY(len < 1 || len == UNIV_SQL_NULL)) {
err_len:
		return("incorrect column length in SYS_TABLES");
	}
	rec_get_nth_field_offs_old(rec, 1/*DB_TRX_ID*/, &len);
	if (UNIV_UNLIKELY(len != DATA_TRX_ID_LEN && len != UNIV_SQL_NULL)) {
		goto err_len;
	}
	rec_get_nth_field_offs_old(rec, 2/*DB_ROLL_PTR*/, &len);
	if (UNIV_UNLIKELY(len != DATA_ROLL_PTR_LEN && len != UNIV_SQL_NULL)) {
		goto err_len;
	}

	rec_get_nth_field_offs_old(rec, 3/*ID*/, &len);
	if (UNIV_UNLIKELY(len != 8)) {
		goto err_len;
	}

	field = rec_get_nth_field_old(rec, 4/*N_COLS*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}

	n_cols = mach_read_from_4(field);

	rec_get_nth_field_offs_old(rec, 5/*TYPE*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}

	rec_get_nth_field_offs_old(rec, 6/*MIX_ID*/, &len);
	if (UNIV_UNLIKELY(len != 8)) {
		goto err_len;
	}

	rec_get_nth_field_offs_old(rec, 7/*MIX_LEN*/, &len);
	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}

	rec_get_nth_field_offs_old(rec, 8/*CLUSTER_ID*/, &len);
	if (UNIV_UNLIKELY(len != UNIV_SQL_NULL)) {
		goto err_len;
	}

	field = rec_get_nth_field_old(rec, 9/*SPACE*/, &len);

	if (UNIV_UNLIKELY(len != 4)) {
		goto err_len;
	}

	space = mach_read_from_4(field);

	/* Check if the tablespace exists and has the right name */
	if (space != 0) {
		flags = dict_sys_tables_get_flags(rec);

		if (UNIV_UNLIKELY(flags == ULINT_UNDEFINED)) {
			field = rec_get_nth_field_old(rec, 5/*TYPE*/, &len);
			ut_ad(len == 4); /* this was checked earlier */
			flags = mach_read_from_4(field);

			ut_print_timestamp(stderr);
			fputs("  InnoDB: Error: table ", stderr);
			ut_print_filename(stderr, name);
			fprintf(stderr, "\n"
				"InnoDB: in InnoDB data dictionary"
				" has unknown type %lx.\n",
				(ulong) flags);
			return(NULL);
		}
	} else {
		flags = 0;
	}

	/* The high-order bit of N_COLS is the "compact format" flag.
	For tables in that format, MIX_LEN may hold additional flags. */
	if (n_cols & 0x80000000UL) {
		ulint	flags2;

		flags |= DICT_TF_COMPACT;

		field = rec_get_nth_field_old(rec, 7, &len);

		if (UNIV_UNLIKELY(len != 4)) {

			goto err_len;
		}

		flags2 = mach_read_from_4(field);

		if (flags2 & (~0 << (DICT_TF2_BITS - DICT_TF2_SHIFT))) {
			ut_print_timestamp(stderr);
			fputs("  InnoDB: Warning: table ", stderr);
			ut_print_filename(stderr, name);
			fprintf(stderr, "\n"
				"InnoDB: in InnoDB data dictionary"
				" has unknown flags %lx.\n",
				(ulong) flags2);

			flags2 &= ~(~0 << (DICT_TF2_BITS - DICT_TF2_SHIFT));
		}

		flags |= flags2 << DICT_TF2_SHIFT;
	}

	/* See if the tablespace is available. */
	*table = dict_mem_table_create(name, space, n_cols & ~0x80000000UL,
				       flags);

	field = rec_get_nth_field_old(rec, 3/*ID*/, &len);
	ut_ad(len == 8); /* this was checked earlier */

	(*table)->id = mach_read_from_8(field);

	(*table)->ibd_file_missing = FALSE;

	return(NULL);
}

/********************************************************************//**
Loads a table definition and also all its index definitions, and also
the cluster definition if the table is a member in a cluster. Also loads
all foreign key constraints where the foreign key is in the table or where
a foreign key references columns in this table. Adds all these to the data
dictionary cache.
@return table, NULL if does not exist; if the table is stored in an
.ibd file, but the file does not exist, then we set the
ibd_file_missing flag TRUE in the table object we return */
UNIV_INTERN
dict_table_t*
dict_load_table(
/*============*/
	const char*	name,	/*!< in: table name in the
				databasename/tablename format */
	ibool		cached)	/*!< in: TRUE=add to cache, FALSE=do not */
{
	dict_table_t*	table;
	dict_table_t*	sys_tables;
	btr_pcur_t	pcur;
	dict_index_t*	sys_index;
	dtuple_t*	tuple;
	mem_heap_t*	heap;
	dfield_t*	dfield;
	const rec_t*	rec;
	const byte*	field;
	ulint		len;
	ulint		err;
	const char*	err_msg;
	mtr_t		mtr;

	ut_ad(mutex_own(&(dict_sys->mutex)));

	heap = mem_heap_create(32000);

	mtr_start(&mtr);

	sys_tables = dict_table_get_low("SYS_TABLES");
	sys_index = UT_LIST_GET_FIRST(sys_tables->indexes);
	ut_a(!dict_table_is_comp(sys_tables));
	ut_a(name_of_col_is(sys_tables, sys_index, 3, "ID"));
	ut_a(name_of_col_is(sys_tables, sys_index, 4, "N_COLS"));
	ut_a(name_of_col_is(sys_tables, sys_index, 5, "TYPE"));
	ut_a(name_of_col_is(sys_tables, sys_index, 7, "MIX_LEN"));
	ut_a(name_of_col_is(sys_tables, sys_index, 9, "SPACE"));

	tuple = dtuple_create(heap, 1);
	dfield = dtuple_get_nth_field(tuple, 0);

	dfield_set_data(dfield, name, ut_strlen(name));
	dict_index_copy_types(tuple, sys_index, 1);

	btr_pcur_open_on_user_rec(sys_index, tuple, PAGE_CUR_GE,
				  BTR_SEARCH_LEAF, &pcur, &mtr);
	rec = btr_pcur_get_rec(&pcur);

	if (!btr_pcur_is_on_user_rec(&pcur)
	    || rec_get_deleted_flag(rec, 0)) {
		/* Not found */
err_exit:
		btr_pcur_close(&pcur);
		mtr_commit(&mtr);
		mem_heap_free(heap);

		return(NULL);
	}

	field = rec_get_nth_field_old(rec, 0, &len);

	/* Check if the table name in record is the searched one */
	if (len != ut_strlen(name) || ut_memcmp(name, field, len) != 0) {

		goto err_exit;
	}

	err_msg = dict_load_table_low(name, rec, &table);

	if (err_msg) {

		ut_print_timestamp(stderr);
		fprintf(stderr, "  InnoDB: %s\n", err_msg);
		goto err_exit;
	}

	if (table->space == 0) {
		/* The system tablespace is always available. */
	} else if (!fil_space_for_table_exists_in_mem(
			   table->space, name,
			   (table->flags >> DICT_TF2_SHIFT)
			   & DICT_TF2_TEMPORARY,
			   FALSE, FALSE)) {

		if (table->flags & (DICT_TF2_TEMPORARY << DICT_TF2_SHIFT)) {
			/* Do not bother to retry opening temporary tables. */
			table->ibd_file_missing = TRUE;
		} else {
			ut_print_timestamp(stderr);
			fprintf(stderr,
				"  InnoDB: error: space object of table ");
			ut_print_filename(stderr, name);
			fprintf(stderr, ",\n"
				"InnoDB: space id %lu did not exist in memory."
				" Retrying an open.\n",
				(ulong) table->space);
			/* Try to open the tablespace */
			if (!fil_open_single_table_tablespace(
				TRUE, table->space,
				table->flags & ~(~0 << DICT_TF_BITS), name)) {
				/* We failed to find a sensible
				tablespace file */

				table->ibd_file_missing = TRUE;
			}
		}
	}

	btr_pcur_close(&pcur);
	mtr_commit(&mtr);

	dict_load_columns(table, heap);

	if (cached) {
		dict_table_add_to_cache(table, heap);
	} else {
		dict_table_add_system_columns(table, heap);
	}

	mem_heap_empty(heap);

	err = dict_load_indexes(table, heap);

	/* If the force recovery flag is set, we open the table irrespective
	of the error condition, since the user may want to dump data from the
	clustered index. However we load the foreign key information only if
	all indexes were loaded. */
	if (!cached) {
	} else if (err == DB_SUCCESS) {
		err = dict_load_foreigns(table->name, TRUE);
	} else if (!srv_force_recovery) {
		dict_table_remove_from_cache(table);
		table = NULL;
	}
#if 0
	if (err != DB_SUCCESS && table != NULL) {

		mutex_enter(&dict_foreign_err_mutex);

		ut_print_timestamp(stderr);

		fprintf(stderr,
			"  InnoDB: Error: could not make a foreign key"
			" definition to match\n"
			"InnoDB: the foreign key table"
			" or the referenced table!\n"
			"InnoDB: The data dictionary of InnoDB is corrupt."
			" You may need to drop\n"
			"InnoDB: and recreate the foreign key table"
			" or the referenced table.\n"
			"InnoDB: Submit a detailed bug report"
			" to http://bugs.mysql.com\n"
			"InnoDB: Latest foreign key error printout:\n%s\n",
			dict_foreign_err_buf);

		mutex_exit(&dict_foreign_err_mutex);
	}
#endif /* 0 */
	mem_heap_free(heap);

	return(table);
}

/***********************************************************************//**
Loads a table object based on the table id.
@return	table; NULL if table does not exist */
UNIV_INTERN
dict_table_t*
dict_load_table_on_id(
/*==================*/
	dulint	table_id)	/*!< in: table id */
{
	byte		id_buf[8];
	btr_pcur_t	pcur;
	mem_heap_t*	heap;
	dtuple_t*	tuple;
	dfield_t*	dfield;
	dict_index_t*	sys_table_ids;
	dict_table_t*	sys_tables;
	const rec_t*	rec;
	const byte*	field;
	ulint		len;
	dict_table_t*	table;
	mtr_t		mtr;

	ut_ad(mutex_own(&(dict_sys->mutex)));

	/* NOTE that the operation of this function is protected by
	the dictionary mutex, and therefore no deadlocks can occur
	with other dictionary operations. */

	mtr_start(&mtr);
	/*---------------------------------------------------*/
	/* Get the secondary index based on ID for table SYS_TABLES */
	sys_tables = dict_sys->sys_tables;
	sys_table_ids = dict_table_get_next_index(
		dict_table_get_first_index(sys_tables));
	ut_a(!dict_table_is_comp(sys_tables));
	heap = mem_heap_create(256);

	tuple  = dtuple_create(heap, 1);
	dfield = dtuple_get_nth_field(tuple, 0);

	/* Write the table id in byte format to id_buf */
	mach_write_to_8(id_buf, table_id);

	dfield_set_data(dfield, id_buf, 8);
	dict_index_copy_types(tuple, sys_table_ids, 1);

	btr_pcur_open_on_user_rec(sys_table_ids, tuple, PAGE_CUR_GE,
				  BTR_SEARCH_LEAF, &pcur, &mtr);
	rec = btr_pcur_get_rec(&pcur);

	if (!btr_pcur_is_on_user_rec(&pcur)
	    || rec_get_deleted_flag(rec, 0)) {
		/* Not found */

		btr_pcur_close(&pcur);
		mtr_commit(&mtr);
		mem_heap_free(heap);

		return(NULL);
	}

	/*---------------------------------------------------*/
	/* Now we have the record in the secondary index containing the
	table ID and NAME */

	rec = btr_pcur_get_rec(&pcur);
	field = rec_get_nth_field_old(rec, 0, &len);
	ut_ad(len == 8);

	/* Check if the table id in record is the one searched for */
	if (ut_dulint_cmp(table_id, mach_read_from_8(field)) != 0) {

		btr_pcur_close(&pcur);
		mtr_commit(&mtr);
		mem_heap_free(heap);

		return(NULL);
	}

	/* Now we get the table name from the record */
	field = rec_get_nth_field_old(rec, 1, &len);
	/* Load the table definition to memory */
	table = dict_load_table(mem_heap_strdupl(heap, (char*) field, len),
				TRUE);

	btr_pcur_close(&pcur);
	mtr_commit(&mtr);
	mem_heap_free(heap);

	return(table);
}

/********************************************************************//**
This function is called when the database is booted. Loads system table
index definitions except for the clustered index which is added to the
dictionary cache at booting before calling this function. */
UNIV_INTERN
void
dict_load_sys_table(
/*================*/
	dict_table_t*	table)	/*!< in: system table */
{
	mem_heap_t*	heap;

	ut_ad(mutex_own(&(dict_sys->mutex)));

	heap = mem_heap_create(1000);

	dict_load_indexes(table, heap);

	mem_heap_free(heap);
}

/********************************************************************//**
Loads foreign key constraint col names (also for the referenced table). */
static
void
dict_load_foreign_cols(
/*===================*/
	const char*	id,	/*!< in: foreign constraint id as a
				null-terminated string */
	dict_foreign_t*	foreign)/*!< in: foreign constraint object */
{
	dict_table_t*	sys_foreign_cols;
	dict_index_t*	sys_index;
	btr_pcur_t	pcur;
	dtuple_t*	tuple;
	dfield_t*	dfield;
	const rec_t*	rec;
	const byte*	field;
	ulint		len;
	ulint		i;
	mtr_t		mtr;

	ut_ad(mutex_own(&(dict_sys->mutex)));

	foreign->foreign_col_names = mem_heap_alloc(
		foreign->heap, foreign->n_fields * sizeof(void*));

	foreign->referenced_col_names = mem_heap_alloc(
		foreign->heap, foreign->n_fields * sizeof(void*));
	mtr_start(&mtr);

	sys_foreign_cols = dict_table_get_low("SYS_FOREIGN_COLS");
	sys_index = UT_LIST_GET_FIRST(sys_foreign_cols->indexes);
	ut_a(!dict_table_is_comp(sys_foreign_cols));

	tuple = dtuple_create(foreign->heap, 1);
	dfield = dtuple_get_nth_field(tuple, 0);

	dfield_set_data(dfield, id, ut_strlen(id));
	dict_index_copy_types(tuple, sys_index, 1);

	btr_pcur_open_on_user_rec(sys_index, tuple, PAGE_CUR_GE,
				  BTR_SEARCH_LEAF, &pcur, &mtr);
	for (i = 0; i < foreign->n_fields; i++) {

		rec = btr_pcur_get_rec(&pcur);

		ut_a(btr_pcur_is_on_user_rec(&pcur));
		ut_a(!rec_get_deleted_flag(rec, 0));

		field = rec_get_nth_field_old(rec, 0, &len);
		ut_a(len == ut_strlen(id));
		ut_a(ut_memcmp(id, field, len) == 0);

		field = rec_get_nth_field_old(rec, 1, &len);
		ut_a(len == 4);
		ut_a(i == mach_read_from_4(field));

		field = rec_get_nth_field_old(rec, 4, &len);
		foreign->foreign_col_names[i] = mem_heap_strdupl(
			foreign->heap, (char*) field, len);

		field = rec_get_nth_field_old(rec, 5, &len);
		foreign->referenced_col_names[i] = mem_heap_strdupl(
			foreign->heap, (char*) field, len);

		btr_pcur_move_to_next_user_rec(&pcur, &mtr);
	}

	btr_pcur_close(&pcur);
	mtr_commit(&mtr);
}

/***********************************************************************//**
Loads a foreign key constraint to the dictionary cache.
@return	DB_SUCCESS or error code */
static
ulint
dict_load_foreign(
/*==============*/
	const char*	id,	/*!< in: foreign constraint id as a
				null-terminated string */
	ibool		check_charsets)
				/*!< in: TRUE=check charset compatibility */
{
	dict_foreign_t*	foreign;
	dict_table_t*	sys_foreign;
	btr_pcur_t	pcur;
	dict_index_t*	sys_index;
	dtuple_t*	tuple;
	mem_heap_t*	heap2;
	dfield_t*	dfield;
	const rec_t*	rec;
	const byte*	field;
	ulint		len;
	ulint		n_fields_and_type;
	mtr_t		mtr;

	ut_ad(mutex_own(&(dict_sys->mutex)));

	heap2 = mem_heap_create(1000);

	mtr_start(&mtr);

	sys_foreign = dict_table_get_low("SYS_FOREIGN");
	sys_index = UT_LIST_GET_FIRST(sys_foreign->indexes);
	ut_a(!dict_table_is_comp(sys_foreign));

	tuple = dtuple_create(heap2, 1);
	dfield = dtuple_get_nth_field(tuple, 0);

	dfield_set_data(dfield, id, ut_strlen(id));
	dict_index_copy_types(tuple, sys_index, 1);

	btr_pcur_open_on_user_rec(sys_index, tuple, PAGE_CUR_GE,
				  BTR_SEARCH_LEAF, &pcur, &mtr);
	rec = btr_pcur_get_rec(&pcur);

	if (!btr_pcur_is_on_user_rec(&pcur)
	    || rec_get_deleted_flag(rec, 0)) {
		/* Not found */

		fprintf(stderr,
			"InnoDB: Error A: cannot load foreign constraint %s\n",
			id);

		btr_pcur_close(&pcur);
		mtr_commit(&mtr);
		mem_heap_free(heap2);

		return(DB_ERROR);
	}

	field = rec_get_nth_field_old(rec, 0, &len);

	/* Check if the id in record is the searched one */
	if (len != ut_strlen(id) || ut_memcmp(id, field, len) != 0) {

		fprintf(stderr,
			"InnoDB: Error B: cannot load foreign constraint %s\n",
			id);

		btr_pcur_close(&pcur);
		mtr_commit(&mtr);
		mem_heap_free(heap2);

		return(DB_ERROR);
	}

	/* Read the table names and the number of columns associated
	with the constraint */

	mem_heap_free(heap2);

	foreign = dict_mem_foreign_create();

	n_fields_and_type = mach_read_from_4(
		rec_get_nth_field_old(rec, 5, &len));

	ut_a(len == 4);

	/* We store the type in the bits 24..29 of n_fields_and_type. */

	foreign->type = (unsigned int) (n_fields_and_type >> 24);
	foreign->n_fields = (unsigned int) (n_fields_and_type & 0x3FFUL);

	foreign->id = mem_heap_strdup(foreign->heap, id);

	field = rec_get_nth_field_old(rec, 3, &len);
	foreign->foreign_table_name = mem_heap_strdupl(
		foreign->heap, (char*) field, len);

	field = rec_get_nth_field_old(rec, 4, &len);
	foreign->referenced_table_name = mem_heap_strdupl(
		foreign->heap, (char*) field, len);

	btr_pcur_close(&pcur);
	mtr_commit(&mtr);

	dict_load_foreign_cols(id, foreign);

	/* If the foreign table is not yet in the dictionary cache, we
	have to load it so that we are able to make type comparisons
	in the next function call. */

	dict_table_get_low(foreign->foreign_table_name);

	/* Note that there may already be a foreign constraint object in
	the dictionary cache for this constraint: then the following
	call only sets the pointers in it to point to the appropriate table
	and index objects and frees the newly created object foreign.
	Adding to the cache should always succeed since we are not creating
	a new foreign key constraint but loading one from the data
	dictionary. */

	return(dict_foreign_add_to_cache(foreign, check_charsets));
}

/***********************************************************************//**
Loads foreign key constraints where the table is either the foreign key
holder or where the table is referenced by a foreign key. Adds these
constraints to the data dictionary. Note that we know that the dictionary
cache already contains all constraints where the other relevant table is
already in the dictionary cache.
@return	DB_SUCCESS or error code */
UNIV_INTERN
ulint
dict_load_foreigns(
/*===============*/
	const char*	table_name,	/*!< in: table name */
	ibool		check_charsets)	/*!< in: TRUE=check charset
					compatibility */
{
	btr_pcur_t	pcur;
	mem_heap_t*	heap;
	dtuple_t*	tuple;
	dfield_t*	dfield;
	dict_index_t*	sec_index;
	dict_table_t*	sys_foreign;
	const rec_t*	rec;
	const byte*	field;
	ulint		len;
	char*		id ;
	ulint		err;
	mtr_t		mtr;

	ut_ad(mutex_own(&(dict_sys->mutex)));

	sys_foreign = dict_table_get_low("SYS_FOREIGN");

	if (sys_foreign == NULL) {
		/* No foreign keys defined yet in this database */

		fprintf(stderr,
			"InnoDB: Error: no foreign key system tables"
			" in the database\n");

		return(DB_ERROR);
	}

	ut_a(!dict_table_is_comp(sys_foreign));
	mtr_start(&mtr);

	/* Get the secondary index based on FOR_NAME from table
	SYS_FOREIGN */

	sec_index = dict_table_get_next_index(
		dict_table_get_first_index(sys_foreign));
start_load:
	heap = mem_heap_create(256);

	tuple  = dtuple_create(heap, 1);
	dfield = dtuple_get_nth_field(tuple, 0);

	dfield_set_data(dfield, table_name, ut_strlen(table_name));
	dict_index_copy_types(tuple, sec_index, 1);

	btr_pcur_open_on_user_rec(sec_index, tuple, PAGE_CUR_GE,
				  BTR_SEARCH_LEAF, &pcur, &mtr);
loop:
	rec = btr_pcur_get_rec(&pcur);

	if (!btr_pcur_is_on_user_rec(&pcur)) {
		/* End of index */

		goto load_next_index;
	}

	/* Now we have the record in the secondary index containing a table
	name and a foreign constraint ID */

	rec = btr_pcur_get_rec(&pcur);
	field = rec_get_nth_field_old(rec, 0, &len);

	/* Check if the table name in the record is the one searched for; the
	following call does the comparison in the latin1_swedish_ci
	charset-collation, in a case-insensitive way. */

	if (0 != cmp_data_data(dfield_get_type(dfield)->mtype,
			       dfield_get_type(dfield)->prtype,
			       dfield_get_data(dfield), dfield_get_len(dfield),
			       field, len)) {

		goto load_next_index;
	}

	/* Since table names in SYS_FOREIGN are stored in a case-insensitive
	order, we have to check that the table name matches also in a binary
	string comparison. On Unix, MySQL allows table names that only differ
	in character case. */

	if (0 != ut_memcmp(field, table_name, len)) {

		goto next_rec;
	}

	if (rec_get_deleted_flag(rec, 0)) {

		goto next_rec;
	}

	/* Now we get a foreign key constraint id */
	field = rec_get_nth_field_old(rec, 1, &len);
	id = mem_heap_strdupl(heap, (char*) field, len);

	btr_pcur_store_position(&pcur, &mtr);

	mtr_commit(&mtr);

	/* Load the foreign constraint definition to the dictionary cache */

	err = dict_load_foreign(id, check_charsets);

	if (err != DB_SUCCESS) {
		btr_pcur_close(&pcur);
		mem_heap_free(heap);

		return(err);
	}

	mtr_start(&mtr);

	btr_pcur_restore_position(BTR_SEARCH_LEAF, &pcur, &mtr);
next_rec:
	btr_pcur_move_to_next_user_rec(&pcur, &mtr);

	goto loop;

load_next_index:
	btr_pcur_close(&pcur);
	mtr_commit(&mtr);
	mem_heap_free(heap);

	sec_index = dict_table_get_next_index(sec_index);

	if (sec_index != NULL) {

		mtr_start(&mtr);

		goto start_load;
	}

	return(DB_SUCCESS);
}
