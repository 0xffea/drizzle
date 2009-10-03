/*****************************************************************************

Copyright (c) 2007, 2009, Innobase Oy. All Rights Reserved.

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

/******************************************************
InnoDB INFORMATION SCHEMA tables interface to MySQL.

Created July 18, 2007 Vasil Dimov
*******************************************************/

#include <drizzled/server_includes.h>
#include <drizzled/error.h>
#include <mystrings/m_ctype.h>
#include <mysys/my_sys.h>
#include <mysys/hash.h>
#include <mysys/mysys_err.h>
#include <drizzled/plugin.h>
#include <drizzled/field.h>
#include <drizzled/table.h>
#include <drizzled/plugin/info_schema_table.h>

#include "i_s.h"


extern "C" {
#include "trx0i_s.h"
#include "trx0trx.h" /* for TRX_QUE_STATE_STR_MAX_LEN */
#include "buf0buddy.h" /* for i_s_cmpmem */
#include "buf0buf.h" /* for buf_pool and PAGE_ZIP_MIN_SIZE */
#include "ha_prototypes.h" /* for innobase_convert_name() */
#include "srv0start.h" /* for srv_was_started */
}
#include "handler0vars.h"

static const char plugin_author[] = "Innobase Oy";

#define OK(expr)		\
	if ((expr) != 0) {	\
		return(1);	\
	}

#define RETURN_IF_INNODB_NOT_STARTED(plugin_name)			\
do {									\
	if (!srv_was_started) {						\
		push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_WARN,	\
				    ER_CANT_FIND_SYSTEM_REC,		\
				    "InnoDB: SELECTing from "		\
				    "INFORMATION_SCHEMA.%s but "	\
				    "the InnoDB storage engine "	\
				    "is not installed", plugin_name);	\
		return(0);						\
	}								\
} while (0)

#define STRUCT_FLD(name, value)	value

drizzled::plugin::InfoSchemaTable *innodb_trx_schema_table= NULL;
drizzled::plugin::InfoSchemaTable *innodb_locks_schema_table= NULL;
drizzled::plugin::InfoSchemaTable *innodb_lock_waits_schema_table= NULL;
drizzled::plugin::InfoSchemaTable *innodb_cmp_schema_table= NULL;
drizzled::plugin::InfoSchemaTable *innodb_cmp_reset_schema_table= NULL;
drizzled::plugin::InfoSchemaTable *innodb_cmpmem_schema_table= NULL;
drizzled::plugin::InfoSchemaTable *innodb_cmpmem_reset_schema_table= NULL;

static TrxISMethods trx_methods;
static CmpISMethods cmp_methods;
static CmpResetISMethods cmp_reset_methods;
static CmpmemISMethods cmpmem_methods;
static CmpmemResetISMethods cmpmem_reset_methods;

/*
Use the following types mapping:

C type	ST_FIELD_INFO::field_type
---------------------------------
long			DRIZZLE_TYPE_LONGLONG
(field_length=MY_INT64_NUM_DECIMAL_DIGITS)

long unsigned		DRIZZLE_TYPE_LONGLONG
(field_length=MY_INT64_NUM_DECIMAL_DIGITS, field_flags=MY_I_S_UNSIGNED)

char*			DRIZZLE_TYPE_STRING
(field_length=n)

float			DRIZZLE_TYPE_FLOAT
(field_length=0 is ignored)

void*			DRIZZLE_TYPE_LONGLONG
(field_length=MY_INT64_NUM_DECIMAL_DIGITS, field_flags=MY_I_S_UNSIGNED)

boolean (if else)	DRIZZLE_TYPE_LONG
(field_length=1)

time_t			DRIZZLE_TYPE_DATETIME
(field_length=0 ignored)
---------------------------------
*/

/* XXX these are defined in mysql_priv.h inside #ifdef DRIZZLE_SERVER */
bool schema_table_store_record(Session *session, Table *table);
void localtime_to_TIME(DRIZZLE_TIME *to, struct tm *from);

/***********************************************************************
Unbind a dynamic INFORMATION_SCHEMA table. */

/***********************************************************************
Auxiliary function to store time_t value in DRIZZLE_TYPE_DATETIME
field. */
static
int
field_store_time_t(
/*===============*/
			/* out: 0 on success */
	Field*	field,	/* in/out: target field for storage */
	time_t	time)	/* in: value to store */
{
	DRIZZLE_TIME	my_time;
	struct tm	tm_time;

#if 0
	/* use this if you are sure that `variables' and `time_zone'
	are always initialized */
	session->variables.time_zone->gmt_sec_to_TIME(
		&my_time, (time_t) time);
#else
	localtime_r(&time, &tm_time);
	localtime_to_TIME(&my_time, &tm_time);
	my_time.time_type = DRIZZLE_TIMESTAMP_DATETIME;
#endif

	return(field->store_time(&my_time, DRIZZLE_TIMESTAMP_DATETIME));
}

/***********************************************************************
Auxiliary function to store char* value in DRIZZLE_TYPE_STRING field. */
static
int
field_store_string(
/*===============*/
				/* out: 0 on success */
	Field*		field,	/* in/out: target field for storage */
	const char*	str)	/* in: NUL-terminated utf-8 string,
				or NULL */
{
	int	ret;

	if (str != NULL) {

		ret = field->store(str, strlen(str),
				   system_charset_info);
		field->set_notnull();
	} else {

		ret = 0; /* success */
		field->set_null();
	}

	return(ret);
}

/***********************************************************************
Auxiliary function to store ulint value in DRIZZLE_TYPE_LONGLONG field.
If the value is ULINT_UNDEFINED then the field it set to NULL. */
static
int
field_store_ulint(
/*==============*/
			/* out: 0 on success */
	Field*	field,	/* in/out: target field for storage */
	ulint	n)	/* in: value to store */
{
	int	ret;

	if (n != ULINT_UNDEFINED) {

		ret = field->store(n);
		field->set_notnull();
	} else {

		ret = 0; /* success */
		field->set_null();
	}

	return(ret);
}

/* Fields of the dynamic table INFORMATION_SCHEMA.innodb_trx */
static drizzled::plugin::ColumnInfo	innodb_trx_fields_info[] =
{
#define IDX_TRX_ID		0
        drizzled::plugin::ColumnInfo("trx_id",
                  TRX_ID_MAX_LEN + 1,
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  0,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_TRX_STATE		1
        drizzled::plugin::ColumnInfo("trx_state",
                  TRX_QUE_STATE_STR_MAX_LEN + 1,
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  0,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_TRX_STARTED		2
        drizzled::plugin::ColumnInfo("trx_started",
                  0,
                  DRIZZLE_TYPE_DATETIME,
                  0,
                  0,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_TRX_REQUESTED_LOCK_ID	3
        drizzled::plugin::ColumnInfo("trx_requested_lock_id",
                  TRX_I_S_LOCK_ID_MAX_LEN + 1,
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  MY_I_S_MAYBE_NULL,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_TRX_WAIT_STARTED	4
        drizzled::plugin::ColumnInfo("trx_wait_started",
                  0,
                  DRIZZLE_TYPE_DATETIME,
                  0,
                  MY_I_S_MAYBE_NULL,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_TRX_WEIGHT		5
        drizzled::plugin::ColumnInfo("trx_weight",
                  MY_INT64_NUM_DECIMAL_DIGITS,
                  DRIZZLE_TYPE_LONGLONG,
                  0,
                  MY_I_S_UNSIGNED,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_TRX_DRIZZLE_THREAD_ID	6
        drizzled::plugin::ColumnInfo("trx_mysql_thread_id",
                  MY_INT64_NUM_DECIMAL_DIGITS,
                  DRIZZLE_TYPE_LONGLONG,
                  0,
                  MY_I_S_UNSIGNED,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_TRX_QUERY		7
        drizzled::plugin::ColumnInfo("trx_query",
                  TRX_I_S_TRX_QUERY_MAX_LEN,
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  MY_I_S_MAYBE_NULL,
                  "",
                  SKIP_OPEN_TABLE),

        drizzled::plugin::ColumnInfo()
};

/***********************************************************************
Read data from cache buffer and fill the INFORMATION_SCHEMA.innodb_trx
table with it. */
static
int
fill_innodb_trx_from_cache(
/*=======================*/
					/* out: 0 on success */
	trx_i_s_cache_t*	cache,	/* in: cache to read from */
	Session*			session,	/* in: used to call
					schema_table_store_record() */
	Table*			table)	/* in/out: fill this table */
{
	Field**	fields;
	ulint	rows_num;
	char	lock_id[TRX_I_S_LOCK_ID_MAX_LEN + 1];
	ulint	i;

	fields = table->field;

	rows_num = trx_i_s_cache_get_rows_used(cache,
					       I_S_INNODB_TRX);

	for (i = 0; i < rows_num; i++) {

		i_s_trx_row_t*	row;
		char		trx_id[TRX_ID_MAX_LEN + 1];

		row = (i_s_trx_row_t*)
			trx_i_s_cache_get_nth_row(
				cache, I_S_INNODB_TRX, i);

		/* trx_id */
		ut_snprintf(trx_id, sizeof(trx_id), TRX_ID_FMT, row->trx_id);
		OK(field_store_string(fields[IDX_TRX_ID], trx_id));

		/* trx_state */
		OK(field_store_string(fields[IDX_TRX_STATE],
				      row->trx_state));

		/* trx_started */
		OK(field_store_time_t(fields[IDX_TRX_STARTED],
				      (time_t) row->trx_started));

		/* trx_requested_lock_id */
		/* trx_wait_started */
		if (row->trx_wait_started != 0) {

			OK(field_store_string(
				   fields[IDX_TRX_REQUESTED_LOCK_ID],
				   trx_i_s_create_lock_id(
					   row->requested_lock_row,
					   lock_id, sizeof(lock_id))));
			/* field_store_string() sets it no notnull */

			OK(field_store_time_t(
				   fields[IDX_TRX_WAIT_STARTED],
				   (time_t) row->trx_wait_started));
			fields[IDX_TRX_WAIT_STARTED]->set_notnull();
		} else {

			fields[IDX_TRX_REQUESTED_LOCK_ID]->set_null();
			fields[IDX_TRX_WAIT_STARTED]->set_null();
		}

		/* trx_weight */
		OK(fields[IDX_TRX_WEIGHT]->store((int64_t) row->trx_weight,
						 true));

		/* trx_mysql_thread_id */
		OK(fields[IDX_TRX_DRIZZLE_THREAD_ID]->store(
			   row->trx_mysql_thread_id));

		/* trx_query */
		OK(field_store_string(fields[IDX_TRX_QUERY],
				      row->trx_query));

		OK(schema_table_store_record(session, table));
	}

	return(0);
}

/***********************************************************************
Bind the dynamic table INFORMATION_SCHEMA.innodb_trx */
int
innodb_trx_init(
/*============*/
			/* out: 0 on success */
	)	/* in/out: table schema object */
{
	if ((innodb_trx_schema_table= new drizzled::plugin::InfoSchemaTable) == NULL)
		return(1);

	innodb_trx_schema_table->setColumnInfo(innodb_trx_fields_info);
	innodb_trx_schema_table->setInfoSchemaMethods(&trx_methods);
	innodb_trx_schema_table->setTableName("INNODB_TRX");

	return(0);
}


/* Fields of the dynamic table INFORMATION_SCHEMA.innodb_locks */
static drizzled::plugin::ColumnInfo innodb_locks_fields_info[] =
{
#define IDX_LOCK_ID		0
        drizzled::plugin::ColumnInfo("lock_id",
                  TRX_I_S_LOCK_ID_MAX_LEN + 1,
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  0,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_LOCK_TRX_ID		1
        drizzled::plugin::ColumnInfo("lock_trx_id",
                  TRX_ID_MAX_LEN + 1,
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  0,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_LOCK_MODE		2
        drizzled::plugin::ColumnInfo("lock_mode",
	 /* S[,GAP] X[,GAP] IS[,GAP] IX[,GAP] AUTO_INC UNKNOWN */
                  32,
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  0,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_LOCK_TYPE		3
        drizzled::plugin::ColumnInfo("lock_type",
                  32, /* RECORD|TABLE|UNKNOWN */
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  0,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_LOCK_TABLE		4
        drizzled::plugin::ColumnInfo("lock_table",
                  1024,
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  0,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_LOCK_INDEX		5
        drizzled::plugin::ColumnInfo("lock_index",
                  1024,
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  MY_I_S_MAYBE_NULL,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_LOCK_SPACE		6
        drizzled::plugin::ColumnInfo("lock_space",
                  MY_INT64_NUM_DECIMAL_DIGITS,
                  DRIZZLE_TYPE_LONGLONG,
                  0,
                  MY_I_S_UNSIGNED | MY_I_S_MAYBE_NULL,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_LOCK_PAGE		7
        drizzled::plugin::ColumnInfo("lock_page",
                  MY_INT64_NUM_DECIMAL_DIGITS,
                  DRIZZLE_TYPE_LONGLONG,
                  0,
                  MY_I_S_UNSIGNED | MY_I_S_MAYBE_NULL,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_LOCK_REC		8
        drizzled::plugin::ColumnInfo("lock_rec",
                  MY_INT64_NUM_DECIMAL_DIGITS,
                  DRIZZLE_TYPE_LONGLONG,
                  0,
                  MY_I_S_UNSIGNED | MY_I_S_MAYBE_NULL,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_LOCK_DATA		9
        drizzled::plugin::ColumnInfo("lock_data",
                  TRX_I_S_LOCK_DATA_MAX_LEN,
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  MY_I_S_MAYBE_NULL,
                  "",
                  SKIP_OPEN_TABLE),

        drizzled::plugin::ColumnInfo()
};

/***********************************************************************
Read data from cache buffer and fill the INFORMATION_SCHEMA.innodb_locks
table with it. */
static
int
fill_innodb_locks_from_cache(
/*=========================*/
					/* out: 0 on success */
	trx_i_s_cache_t*	cache,	/* in: cache to read from */
	Session*			session,	/* in: MySQL client connection */
	Table*			table)	/* in/out: fill this table */
{
	Field**	fields;
	ulint	rows_num;
	char	lock_id[TRX_I_S_LOCK_ID_MAX_LEN + 1];
	ulint	i;

	fields = table->field;

	rows_num = trx_i_s_cache_get_rows_used(cache,
					       I_S_INNODB_LOCKS);

	for (i = 0; i < rows_num; i++) {

		i_s_locks_row_t*	row;

		/* note that the decoded database or table name is
		never expected to be longer than NAME_LEN;
		NAME_LEN for database name
		2 for surrounding quotes around database name
		NAME_LEN for table name
		2 for surrounding quotes around table name
		1 for the separating dot (.)
		9 for the #mysql50# prefix */
		char			buf[2 * NAME_LEN + 14];
		const char*		bufend;

		char			lock_trx_id[TRX_ID_MAX_LEN + 1];

		row = (i_s_locks_row_t*)
			trx_i_s_cache_get_nth_row(
				cache, I_S_INNODB_LOCKS, i);

		/* lock_id */
		trx_i_s_create_lock_id(row, lock_id, sizeof(lock_id));
		OK(field_store_string(fields[IDX_LOCK_ID],
				      lock_id));

		/* lock_trx_id */
		ut_snprintf(lock_trx_id, sizeof(lock_trx_id),
			    TRX_ID_FMT, row->lock_trx_id);
		OK(field_store_string(fields[IDX_LOCK_TRX_ID], lock_trx_id));

		/* lock_mode */
		OK(field_store_string(fields[IDX_LOCK_MODE],
				      row->lock_mode));

		/* lock_type */
		OK(field_store_string(fields[IDX_LOCK_TYPE],
				      row->lock_type));

		/* lock_table */
		bufend = innobase_convert_name(buf, sizeof(buf),
					       row->lock_table,
					       strlen(row->lock_table),
					       session, TRUE);
		OK(fields[IDX_LOCK_TABLE]->store(buf, bufend - buf,
						 system_charset_info));

		/* lock_index */
		if (row->lock_index != NULL) {

			bufend = innobase_convert_name(buf, sizeof(buf),
						       row->lock_index,
						       strlen(row->lock_index),
						       session, FALSE);
			OK(fields[IDX_LOCK_INDEX]->store(buf, bufend - buf,
							 system_charset_info));
			fields[IDX_LOCK_INDEX]->set_notnull();
		} else {

			fields[IDX_LOCK_INDEX]->set_null();
		}

		/* lock_space */
		OK(field_store_ulint(fields[IDX_LOCK_SPACE],
				     row->lock_space));

		/* lock_page */
		OK(field_store_ulint(fields[IDX_LOCK_PAGE],
				     row->lock_page));

		/* lock_rec */
		OK(field_store_ulint(fields[IDX_LOCK_REC],
				     row->lock_rec));

		/* lock_data */
		OK(field_store_string(fields[IDX_LOCK_DATA],
				      row->lock_data));

		OK(schema_table_store_record(session, table));
	}

	return(0);
}

/***********************************************************************
Bind the dynamic table INFORMATION_SCHEMA.innodb_locks */
int
innodb_locks_init(
/*==============*/
			/* out: 0 on success */
	)	/* in/out: table schema object */
{

	if ((innodb_locks_schema_table= new drizzled::plugin::InfoSchemaTable) == NULL)
		return(1);

	innodb_locks_schema_table->setColumnInfo(innodb_locks_fields_info);
	innodb_locks_schema_table->setInfoSchemaMethods(&trx_methods);
	innodb_locks_schema_table->setTableName("INNODB_LOCKS");
	return(0);
}


/* Fields of the dynamic table INFORMATION_SCHEMA.innodb_lock_waits */
static drizzled::plugin::ColumnInfo innodb_lock_waits_fields_info[] =
{
#define IDX_REQUESTING_TRX_ID	0
        drizzled::plugin::ColumnInfo("requesting_trx_id",
                  TRX_ID_MAX_LEN + 1,
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  0,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_REQUESTED_LOCK_ID	1
        drizzled::plugin::ColumnInfo("requested_lock_id",
                  TRX_I_S_LOCK_ID_MAX_LEN + 1,
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  0,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_BLOCKING_TRX_ID	2
        drizzled::plugin::ColumnInfo("blocking_trx_id",
                  TRX_ID_MAX_LEN + 1,
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  0,
                  "",
                  SKIP_OPEN_TABLE),

#define IDX_BLOCKING_LOCK_ID	3
        drizzled::plugin::ColumnInfo("blocking_lock_id",
                  TRX_I_S_LOCK_ID_MAX_LEN + 1,
                  DRIZZLE_TYPE_VARCHAR,
                  0,
                  0,
                  "",
                  SKIP_OPEN_TABLE),

        drizzled::plugin::ColumnInfo()
};

/***********************************************************************
Read data from cache buffer and fill the
INFORMATION_SCHEMA.innodb_lock_waits table with it. */
static
int
fill_innodb_lock_waits_from_cache(
/*==============================*/
					/* out: 0 on success */
	trx_i_s_cache_t*	cache,	/* in: cache to read from */
	Session*			session,	/* in: used to call
					schema_table_store_record() */
	Table*			table)	/* in/out: fill this table */
{
	Field**	fields;
	ulint	rows_num;
	char	requested_lock_id[TRX_I_S_LOCK_ID_MAX_LEN + 1];
	char	blocking_lock_id[TRX_I_S_LOCK_ID_MAX_LEN + 1];
	ulint	i;

	fields = table->field;

	rows_num = trx_i_s_cache_get_rows_used(cache,
					       I_S_INNODB_LOCK_WAITS);

	for (i = 0; i < rows_num; i++) {

		i_s_lock_waits_row_t*	row;

		char	requesting_trx_id[TRX_ID_MAX_LEN + 1];
		char	blocking_trx_id[TRX_ID_MAX_LEN + 1];

		row = (i_s_lock_waits_row_t*)
			trx_i_s_cache_get_nth_row(
				cache, I_S_INNODB_LOCK_WAITS, i);

		/* requesting_trx_id */
		ut_snprintf(requesting_trx_id, sizeof(requesting_trx_id),
			    TRX_ID_FMT, row->requested_lock_row->lock_trx_id);
		OK(field_store_string(fields[IDX_REQUESTING_TRX_ID],
				      requesting_trx_id));

		/* requested_lock_id */
		OK(field_store_string(
			   fields[IDX_REQUESTED_LOCK_ID],
			   trx_i_s_create_lock_id(
				   row->requested_lock_row,
				   requested_lock_id,
				   sizeof(requested_lock_id))));

		/* blocking_trx_id */
		ut_snprintf(blocking_trx_id, sizeof(blocking_trx_id),
			    TRX_ID_FMT, row->blocking_lock_row->lock_trx_id);
		OK(field_store_string(fields[IDX_BLOCKING_TRX_ID],
				      blocking_trx_id));

		/* blocking_lock_id */
		OK(field_store_string(
			   fields[IDX_BLOCKING_LOCK_ID],
			   trx_i_s_create_lock_id(
				   row->blocking_lock_row,
				   blocking_lock_id,
				   sizeof(blocking_lock_id))));

		OK(schema_table_store_record(session, table));
	}

	return(0);
}

/***********************************************************************
Bind the dynamic table INFORMATION_SCHEMA.innodb_lock_waits */
int
innodb_lock_waits_init(
/*===================*/
			/* out: 0 on success */
	)
{

	if ((innodb_lock_waits_schema_table= new drizzled::plugin::InfoSchemaTable) == NULL)
		return(1);

	innodb_lock_waits_schema_table->setColumnInfo(innodb_lock_waits_fields_info);
	innodb_lock_waits_schema_table->setInfoSchemaMethods(&trx_methods);
	innodb_lock_waits_schema_table->setTableName("INNODB_LOCK_WAITS");


	return(0);
}


/***********************************************************************
Common function to fill any of the dynamic tables:
INFORMATION_SCHEMA.innodb_trx
INFORMATION_SCHEMA.innodb_locks
INFORMATION_SCHEMA.innodb_lock_waits */
int
TrxISMethods::fillTable(
/*======================*/
				/* out: 0 on success */
	Session*		session,	/* in: thread */
	TableList*	tables,	/* in/out: tables to fill */
	COND*		)	/* in: condition (not used) */
{
	const char*		table_name;
	int			ret;
	trx_i_s_cache_t*	cache;

	/* minimize the number of places where global variables are
	referenced */
	cache = trx_i_s_cache;

	/* which table we have to fill? */
	table_name = tables->schema_table_name;
	/* or table_name = tables->schema_table->table_name; */

	RETURN_IF_INNODB_NOT_STARTED(table_name);

	/* update the cache */
	trx_i_s_cache_start_write(cache);
	trx_i_s_possibly_fetch_data_into_cache(cache);
	trx_i_s_cache_end_write(cache);

	if (trx_i_s_cache_is_truncated(cache)) {

		/* XXX show warning to user if possible */
		fprintf(stderr, "Warning: data in %s truncated due to "
			"memory limit of %d bytes\n", table_name,
			TRX_I_S_MEM_LIMIT);
	}

	ret = 0;

	trx_i_s_cache_start_read(cache);

	if (innobase_strcasecmp(table_name, "innodb_trx") == 0) {

		if (fill_innodb_trx_from_cache(
			cache, session, tables->table) != 0) {

			ret = 1;
		}

	} else if (innobase_strcasecmp(table_name, "innodb_locks") == 0) {

		if (fill_innodb_locks_from_cache(
			cache, session, tables->table) != 0) {

			ret = 1;
		}

	} else if (innobase_strcasecmp(table_name, "innodb_lock_waits") == 0) {

		if (fill_innodb_lock_waits_from_cache(
			cache, session, tables->table) != 0) {

			ret = 1;
		}

	} else {

		/* huh! what happened!? */
		fprintf(stderr,
			"InnoDB: trx_i_s_common_fill_table() was "
			"called to fill unknown table: %s.\n"
			"This function only knows how to fill "
			"innodb_trx, innodb_locks and "
			"innodb_lock_waits tables.\n", table_name);

		ret = 1;
	}

	trx_i_s_cache_end_read(cache);

#if 0
	return(ret);
#else
	/* if this function returns something else than 0 then a
	deadlock occurs between the mysqld server and mysql client,
	see http://bugs.mysql.com/29900 ; when that bug is resolved
	we can enable the return(ret) above */
	return(0);
#endif
}

/* Fields of the dynamic table information_schema.innodb_cmp. */
static drizzled::plugin::ColumnInfo	i_s_cmp_fields_info[] =
{
        drizzled::plugin::ColumnInfo("page_size",
                  5,
                  DRIZZLE_TYPE_LONG,
                  0,
                  0,
                  "Compressed Page Size",
                  SKIP_OPEN_TABLE),

        drizzled::plugin::ColumnInfo("compress_ops",
                  MY_INT32_NUM_DECIMAL_DIGITS,
                  DRIZZLE_TYPE_LONG,
                  0,
                  0,
                  "Total Number of Compressions",
                  SKIP_OPEN_TABLE),

        drizzled::plugin::ColumnInfo("compress_ops_ok",
                  MY_INT32_NUM_DECIMAL_DIGITS,
                  DRIZZLE_TYPE_LONG,
                  0,
                  0,
                  "Total Number of Successful Compressions",
                  SKIP_OPEN_TABLE),

        drizzled::plugin::ColumnInfo("compress_time",
                  MY_INT32_NUM_DECIMAL_DIGITS,
                  DRIZZLE_TYPE_LONG,
                  0,
                  0,
                  "Total Duration of Compressions in Seconds",
                  SKIP_OPEN_TABLE),

        drizzled::plugin::ColumnInfo("uncompress_ops",
                  MY_INT32_NUM_DECIMAL_DIGITS,
                  DRIZZLE_TYPE_LONG,
                  0,
                  0,
                  "Total Number of Decompressions",
                  SKIP_OPEN_TABLE),

        drizzled::plugin::ColumnInfo("uncompress_time",
                  MY_INT32_NUM_DECIMAL_DIGITS,
                  DRIZZLE_TYPE_LONG,
                  0,
                  0,
                  "Total Duration of Decompressions in Seconds",
                  SKIP_OPEN_TABLE),

        drizzled::plugin::ColumnInfo()
};


/***********************************************************************
Fill the dynamic table information_schema.innodb_cmp or
innodb_cmp_reset. */
static
int
i_s_cmp_fill_low(
/*=============*/
				/* out: 0 on success, 1 on failure */
	Session*		session,	/* in: thread */
	TableList*	tables,	/* in/out: tables to fill */
	COND*		,	/* in: condition (ignored) */
	ibool		reset)	/* in: TRUE=reset cumulated counts */
{
	Table*	table	= (Table *) tables->table;
	int	status	= 0;


	RETURN_IF_INNODB_NOT_STARTED(tables->schema_table_name);

	for (uint i = 0; i < PAGE_ZIP_NUM_SSIZE - 1; i++) {
		page_zip_stat_t*	zip_stat = &page_zip_stat[i];

		table->field[0]->store(PAGE_ZIP_MIN_SIZE << i);

		/* The cumulated counts are not protected by any
		mutex.  Thus, some operation in page0zip.c could
		increment a counter between the time we read it and
		clear it.  We could introduce mutex protection, but it
		could cause a measureable performance hit in
		page0zip.c. */
		table->field[1]->store(zip_stat->compressed);
		table->field[2]->store(zip_stat->compressed_ok);
		table->field[3]->store(
			(ulong) (zip_stat->compressed_usec / 1000000));
		table->field[4]->store(zip_stat->decompressed);
		table->field[5]->store(
			(ulong) (zip_stat->decompressed_usec / 1000000));

		if (reset) {
			memset(zip_stat, 0, sizeof *zip_stat);
		}

		if (schema_table_store_record(session, table)) {
			status = 1;
			break;
		}
	}

	return(status);
}

/***********************************************************************
Fill the dynamic table information_schema.innodb_cmp. */
int
CmpISMethods::fillTable(
/*=========*/
				/* out: 0 on success, 1 on failure */
	Session*		session,	/* in: thread */
	TableList*	tables,	/* in/out: tables to fill */
	COND*		cond)	/* in: condition (ignored) */
{
	return(i_s_cmp_fill_low(session, tables, cond, FALSE));
}

/***********************************************************************
Fill the dynamic table information_schema.innodb_cmp_reset. */
int
CmpResetISMethods::fillTable(
/*===============*/
				/* out: 0 on success, 1 on failure */
	Session*		session,	/* in: thread */
	TableList*	tables,	/* in/out: tables to fill */
	COND*		cond)	/* in: condition (ignored) */
{
	return(i_s_cmp_fill_low(session, tables, cond, TRUE));
}


/***********************************************************************
Bind the dynamic table information_schema.innodb_cmp. */
int
i_s_cmp_init(
/*=========*/
			/* out: 0 on success */
	)
{

	if ((innodb_cmp_schema_table= new drizzled::plugin::InfoSchemaTable) == NULL)
		return(1);

	innodb_cmp_schema_table->setColumnInfo(i_s_cmp_fields_info);
	innodb_cmp_schema_table->setInfoSchemaMethods(&cmp_methods);
	innodb_cmp_schema_table->setTableName("INNODB_CMP");

	return(0);
}

/***********************************************************************
Bind the dynamic table information_schema.innodb_cmp_reset. */
int
i_s_cmp_reset_init(
/*===============*/
			/* out: 0 on success */
	)	/* in/out: table schema object */
{

	if ((innodb_cmp_reset_schema_table= new drizzled::plugin::InfoSchemaTable) == NULL)
		return(1);

	innodb_cmp_reset_schema_table->setColumnInfo(i_s_cmp_fields_info);
	innodb_cmp_reset_schema_table->setInfoSchemaMethods(&cmp_reset_methods);
	innodb_cmp_reset_schema_table->setTableName("INNODB_CMP_RESET");

	return(0);
}



/* Fields of the dynamic table information_schema.innodb_cmpmem. */
static drizzled::plugin::ColumnInfo	i_s_cmpmem_fields_info[] =
{
        drizzled::plugin::ColumnInfo("page_size",
                  5,
                  DRIZZLE_TYPE_LONG,
                  0,
                  0,
                  "Buddy Block Size",
                  SKIP_OPEN_TABLE),

        drizzled::plugin::ColumnInfo("pages_used",
                  MY_INT32_NUM_DECIMAL_DIGITS,
                  DRIZZLE_TYPE_LONG,
                  0,
                  0,
                  "Currently in Use",
                  SKIP_OPEN_TABLE),

        drizzled::plugin::ColumnInfo("pages_free",
                  MY_INT32_NUM_DECIMAL_DIGITS,
                  DRIZZLE_TYPE_LONG,
                  0,
                  0,
                  "Currently Available",
                  SKIP_OPEN_TABLE),

        drizzled::plugin::ColumnInfo("relocation_ops",
                  MY_INT64_NUM_DECIMAL_DIGITS,
                  DRIZZLE_TYPE_LONGLONG,
                  0,
                  0,
                  "Total Number of Relocations",
                  SKIP_OPEN_TABLE),

        drizzled::plugin::ColumnInfo("relocation_time",
                  MY_INT32_NUM_DECIMAL_DIGITS,
                  DRIZZLE_TYPE_LONG,
                  0,
                  0,
                  "Total Duration of Relocations, in Seconds",
                  SKIP_OPEN_TABLE),

        drizzled::plugin::ColumnInfo()
};

/***********************************************************************
Fill the dynamic table information_schema.innodb_cmpmem or
innodb_cmpmem_reset. */
static
int
i_s_cmpmem_fill_low(
/*================*/
				/* out: 0 on success, 1 on failure */
	Session*		session,	/* in: thread */
	TableList*	tables,	/* in/out: tables to fill */
	COND*		,	/* in: condition (ignored) */
	ibool		reset)	/* in: TRUE=reset cumulated counts */
{
	Table*	table	= (Table *) tables->table;
	int	status	= 0;

	RETURN_IF_INNODB_NOT_STARTED(tables->schema_table_name);

	buf_pool_mutex_enter();

	for (uint x = 0; x <= BUF_BUDDY_SIZES; x++) {
		buf_buddy_stat_t*	buddy_stat = &buf_buddy_stat[x];

		table->field[0]->store(BUF_BUDDY_LOW << x);
		table->field[1]->store(buddy_stat->used);
		table->field[2]->store(UNIV_LIKELY(x < BUF_BUDDY_SIZES)
				       ? UT_LIST_GET_LEN(buf_pool->zip_free[x])
				       : 0);
		table->field[3]->store((int64_t) buddy_stat->relocated, true);
		table->field[4]->store(
			(ulong) (buddy_stat->relocated_usec / 1000000));

		if (reset) {
			/* This is protected by buf_pool_mutex. */
			buddy_stat->relocated = 0;
			buddy_stat->relocated_usec = 0;
		}

		if (schema_table_store_record(session, table)) {
			status = 1;
			break;
		}
	}

	buf_pool_mutex_exit();
	return(status);
}

/***********************************************************************
Fill the dynamic table information_schema.innodb_cmpmem. */
int
CmpmemISMethods::fillTable(
/*============*/
				/* out: 0 on success, 1 on failure */
	Session*		session,	/* in: thread */
	TableList*	tables,	/* in/out: tables to fill */
	COND*		cond)	/* in: condition (ignored) */
{
	return(i_s_cmpmem_fill_low(session, tables, cond, FALSE));
}

/***********************************************************************
Fill the dynamic table information_schema.innodb_cmpmem_reset. */
int
CmpmemResetISMethods::fillTable(
/*==================*/
				/* out: 0 on success, 1 on failure */
	Session*		session,	/* in: thread */
	TableList*	tables,	/* in/out: tables to fill */
	COND*		cond)	/* in: condition (ignored) */
{
	return(i_s_cmpmem_fill_low(session, tables, cond, TRUE));
}

/***********************************************************************
Bind the dynamic table information_schema.innodb_cmpmem. */
int
i_s_cmpmem_init(
/*============*/
			/* out: 0 on success */
	)
{

	if ((innodb_cmpmem_schema_table= new drizzled::plugin::InfoSchemaTable) == NULL)
		return(1);

	innodb_cmpmem_schema_table->setColumnInfo(i_s_cmpmem_fields_info);
	innodb_cmpmem_schema_table->setInfoSchemaMethods(&cmpmem_methods);
	innodb_cmpmem_schema_table->setTableName("INNODB_CMPMEM");

	return(0);
}

/***********************************************************************
Bind the dynamic table information_schema.innodb_cmpmem_reset. */
int
i_s_cmpmem_reset_init(
/*==================*/
			/* out: 0 on success */
	)
{
	if ((innodb_cmpmem_reset_schema_table= new drizzled::plugin::InfoSchemaTable) == NULL)
		return(1);

	innodb_cmpmem_reset_schema_table->setColumnInfo(i_s_cmpmem_fields_info);
	innodb_cmpmem_reset_schema_table->setInfoSchemaMethods(&cmpmem_reset_methods);
	innodb_cmpmem_reset_schema_table->setTableName("INNODB_CMPMEM_RESET");

	return(0);
}


/***********************************************************************
Unbind a dynamic INFORMATION_SCHEMA table. */
int
i_s_common_deinit(
/*==============*/
			/* out: 0 on success */
	drizzled::plugin::Registry &registry)	/* in/out: table schema object */
{
	registry.remove(innodb_trx_schema_table);
	registry.remove(innodb_locks_schema_table);
	registry.remove(innodb_lock_waits_schema_table);
	registry.remove(innodb_cmp_schema_table);
	registry.remove(innodb_cmp_reset_schema_table);
	registry.remove(innodb_cmpmem_schema_table);
	registry.remove(innodb_cmpmem_reset_schema_table);

	delete innodb_trx_schema_table;
	delete innodb_locks_schema_table;
	delete innodb_lock_waits_schema_table;
	delete innodb_cmp_schema_table;
	delete innodb_cmp_reset_schema_table;
	delete innodb_cmpmem_schema_table;
	delete innodb_cmpmem_reset_schema_table;

	return(0);
}
