/* Copyright (C) 2000-2004 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/* drop and alter of tables */

#include <drizzled/server_includes.h>
#include <plugin/myisam/myisam.h>
#include <drizzled/show.h>
#include <drizzled/error.h>
#include <drizzled/gettext.h>
#include <drizzled/data_home.h>
#include <drizzled/sql_parse.h>
#include <mysys/hash.h>
#include <drizzled/sql_lex.h>
#include <drizzled/session.h>
#include <drizzled/sql_base.h>
#include <drizzled/db.h>
#include <drizzled/lock.h>
#include <drizzled/unireg.h>
#include <drizzled/item/int.h>
#include <drizzled/item/empty_string.h>
#include <drizzled/replication_services.h>
#include <drizzled/table_proto.h>

#include <algorithm>

using namespace std;
using namespace drizzled;
extern drizzled::ReplicationServices replication_services;

static const char hexchars[]= "0123456789abcdef";
bool is_primary_key(KEY *key_info)
{
  static const char * primary_key_name="PRIMARY";
  return (strcmp(key_info->name, primary_key_name)==0);
}

const char* is_primary_key_name(const char* key_name)
{
  static const char * primary_key_name="PRIMARY";
  if (strcmp(key_name, primary_key_name)==0)
    return key_name;
  else
    return NULL;
}

static bool check_if_keyname_exists(const char *name,KEY *start, KEY *end);
static char *make_unique_key_name(const char *field_name,KEY *start,KEY *end);
static int copy_data_between_tables(Table *from,Table *to,
                                    List<CreateField> &create, bool ignore,
                                    uint32_t order_num, order_st *order,
                                    ha_rows *copied,ha_rows *deleted,
                                    enum enum_enable_or_disable keys_onoff,
                                    bool error_if_not_empty);

static bool prepare_blob_field(Session *session, CreateField *sql_field);
static bool check_engine(Session *, const char *, HA_CREATE_INFO *);
static int
mysql_prepare_create_table(Session *session, HA_CREATE_INFO *create_info,
                           Alter_info *alter_info,
                           bool tmp_table,
                               uint32_t *db_options,
                               handler *file, KEY **key_info_buffer,
                               uint32_t *key_count, int select_field_count);
static bool
mysql_prepare_alter_table(Session *session, Table *table,
                          HA_CREATE_INFO *create_info,
                          Alter_info *alter_info);

static void set_table_default_charset(HA_CREATE_INFO *create_info, char *db)
{
  /*
    If the table character set was not given explicitly,
    let's fetch the database default character set and
    apply it to the table.
  */
  if (create_info->default_table_charset == NULL)
    create_info->default_table_charset= get_default_db_collation(db);
}

/*
  Translate a file name to a table name (WL #1324).

  SYNOPSIS
    filename_to_tablename()
      from                      The file name
      to                OUT     The table name
      to_length                 The size of the table name buffer.

  RETURN
    Table name length.
*/
uint32_t filename_to_tablename(const char *from, char *to, uint32_t to_length)
{
  uint32_t length= 0;

  if (!memcmp(from, TMP_FILE_PREFIX, TMP_FILE_PREFIX_LENGTH))
  {
    /* Temporary table name. */
    length= strlen(strncpy(to, from, to_length));
  }
  else
  {
    for (; *from  && length < to_length; length++, from++)
    {
      if (*from != '@')
      {
        to[length]= *from;
        continue;
      }
      /* We've found an escaped char - skip the @ */
      from++;
      to[length]= 0;
      /* There will be a two-position hex-char version of the char */
      for (int x=1; x >= 0; x--)
      {
        if (*from >= '0' && *from <= '9')
          to[length] += ((*from++ - '0') << (4 * x));
        else if (*from >= 'a' && *from <= 'f')
          to[length] += ((*from++ - 'a' + 10) << (4 * x));
      }
      /* Backup because we advanced extra in the inner loop */
      from--;
    } 
  }

  return length;
}


/*
  Translate a table name to a file name (WL #1324).

  SYNOPSIS
    tablename_to_filename()
      from                      The table name
      to                OUT     The file name
      to_length                 The size of the file name buffer.

  RETURN
    true if errors happen. false on success.
*/
bool tablename_to_filename(const char *from, char *to, size_t to_length)
{
  
  size_t length= 0;
  for (; *from  && length < to_length; length++, from++)
  {
    if ((*from >= '0' && *from <= '9') ||
        (*from >= 'A' && *from <= 'Z') ||
        (*from >= 'a' && *from <= 'z') ||
/* OSX defines an extra set of high-bit and multi-byte characters
   that cannot be used on the filesystem. Instead of trying to sort
   those out, we'll just escape encode all high-bit-set chars on OSX.
   It won't really hurt anything - it'll just make some filenames ugly. */
#if !defined(TARGET_OS_OSX)
        ((unsigned char)*from >= 128) ||
#endif
        (*from == '_') ||
        (*from == ' ') ||
        (*from == '-'))
    {
      to[length]= *from;
      continue;
    }
   
    if (length + 3 >= to_length)
      return true;

    /* We need to escape this char in a way that can be reversed */
    to[length++]= '@';
    to[length++]= hexchars[(*from >> 4) & 15];
    to[length]= hexchars[(*from) & 15];
  }

  if (check_if_legal_tablename(to) &&
      length + 4 < to_length)
  {
    memcpy(to + length, "@@@", 4);
    length+= 3;
  }
  return false;
}


/*
  Creates path to a file: drizzle_data_dir/db/table.ext

  SYNOPSIS
   build_table_filename()
     buff                       Where to write result
                                This may be the same as table_name.
     bufflen                    buff size
     db                         Database name
     table_name                 Table name
     ext                        File extension.
     flags                      FN_FROM_IS_TMP or FN_TO_IS_TMP or FN_IS_TMP
                                table_name is temporary, do not change.

  NOTES

    Uses database and table name, and extension to create
    a file name in drizzle_data_dir. Database and table
    names are converted from system_charset_info into "fscs".
    Unless flags indicate a temporary table name.
    'db' is always converted.
    'ext' is not converted.

    The conversion suppression is required for ALTER Table. This
    statement creates intermediate tables. These are regular
    (non-temporary) tables with a temporary name. Their path names must
    be derivable from the table name. So we cannot use
    build_tmptable_filename() for them.

  RETURN
    path length on success, 0 on failure
*/

size_t build_table_filename(char *buff, size_t bufflen, const char *db, const char *table_name, bool is_tmp)
{
  char dbbuff[FN_REFLEN];
  char tbbuff[FN_REFLEN];
  bool conversion_error= false;

  memset(tbbuff, 0, sizeof(tbbuff));
  if (is_tmp) // FN_FROM_IS_TMP | FN_TO_IS_TMP
    strncpy(tbbuff, table_name, sizeof(tbbuff));
  else
  {
    conversion_error= tablename_to_filename(table_name, tbbuff, sizeof(tbbuff));
    if (conversion_error)
    {
      errmsg_printf(ERRMSG_LVL_ERROR,
                    _("Table name cannot be encoded and fit within filesystem "
                      "name length restrictions."));
      return 0;
    }
  }
  memset(dbbuff, 0, sizeof(dbbuff));
  conversion_error= tablename_to_filename(db, dbbuff, sizeof(dbbuff));
  if (conversion_error)
  {
    errmsg_printf(ERRMSG_LVL_ERROR,
                  _("Schema name cannot be encoded and fit within filesystem "
                    "name length restrictions."));
    return 0;
  }
   

  int rootdir_len= strlen(FN_ROOTDIR);
  string table_path(drizzle_data_home);
  int without_rootdir= table_path.length()-rootdir_len;

  /* Don't add FN_ROOTDIR if dirzzle_data_home already includes it */
  if (without_rootdir >= 0)
  {
    const char *tmp= table_path.c_str()+without_rootdir;
    if (memcmp(tmp, FN_ROOTDIR, rootdir_len) != 0)
      table_path.append(FN_ROOTDIR);
  }

  table_path.append(dbbuff);
  table_path.append(FN_ROOTDIR);
  table_path.append(tbbuff);

  if (bufflen < table_path.length())
    return 0;

  strcpy(buff, table_path.c_str());

  return table_path.length();
}


/*
  Creates path to a file: drizzle_tmpdir/#sql1234_12_1.ext

  SYNOPSIS
   build_tmptable_filename()
     session                    The thread handle.
     buff                       Where to write result
     bufflen                    buff size

  NOTES

    Uses current_pid, thread_id, and tmp_table counter to create
    a file name in drizzle_tmpdir.

  RETURN
    path length on success, 0 on failure
*/

static uint32_t build_tmptable_filename(Session* session,
                                        char *buff, size_t bufflen)
{
  uint32_t length;
  ostringstream path_str, post_tmpdir_str;
  string tmp;

  path_str << drizzle_tmpdir;
  post_tmpdir_str << "/" << TMP_FILE_PREFIX << current_pid;
  post_tmpdir_str << session->thread_id << session->tmp_table++;
  tmp= post_tmpdir_str.str();

  transform(tmp.begin(), tmp.end(), tmp.begin(), ::tolower);

  path_str << tmp;

  if (bufflen < path_str.str().length())
    length= 0;
  else
    length= unpack_filename(buff, path_str.str().c_str());

  return length;
}

/*
  SYNOPSIS
    write_bin_log()
    session                           Thread object
    clear_error                   is clear_error to be called
    query                         Query to log
    query_length                  Length of query

  RETURN VALUES
    NONE

  DESCRIPTION
    Write the binlog if open, routine used in multiple places in this
    file
*/

void write_bin_log(Session *session, bool,
                   char const *query, size_t query_length)
{
  replication_services.rawStatement(session, query, query_length);
}


/*
 delete (drop) tables.

  SYNOPSIS
   mysql_rm_table()
   session			Thread handle
   tables		List of tables to delete
   if_exists		If 1, don't give error if one table doesn't exists

  NOTES
    Will delete all tables that can be deleted and give a compact error
    messages for tables that could not be deleted.
    If a table is in use, we will wait for all users to free the table
    before dropping it

    Wait if global_read_lock (FLUSH TABLES WITH READ LOCK) is set, but
    not if under LOCK TABLES.

  RETURN
    false OK.  In this case ok packet is sent to user
    true  Error

*/

bool mysql_rm_table(Session *session,TableList *tables, bool if_exists, bool drop_temporary)
{
  bool error, need_start_waiting= false;

  if (tables && tables->schema_table)
  {
    my_error(ER_DBACCESS_DENIED_ERROR, MYF(0), "", "", INFORMATION_SCHEMA_NAME.c_str());
    return(true);
  }

  /* mark for close and remove all cached entries */

  if (!drop_temporary)
  {
    if (!(need_start_waiting= !wait_if_global_read_lock(session, 0, 1)))
      return(true);
  }

  /*
    Acquire LOCK_open after wait_if_global_read_lock(). If we would hold
    LOCK_open during wait_if_global_read_lock(), other threads could not
    close their tables. This would make a pretty deadlock.
  */
  error= mysql_rm_table_part2(session, tables, if_exists, drop_temporary, 0);

  if (need_start_waiting)
    start_waiting_global_read_lock(session);

  if (error)
    return(true);
  session->my_ok();
  return(false);
}

/*
  Execute the drop of a normal or temporary table

  SYNOPSIS
    mysql_rm_table_part2()
    session			Thread handler
    tables		Tables to drop
    if_exists		If set, don't give an error if table doesn't exists.
			In this case we give an warning of level 'NOTE'
    drop_temporary	Only drop temporary tables
    drop_view		Allow to delete VIEW .frm
    dont_log_query	Don't write query to log files. This will also not
			generate warnings if the handler files doesn't exists

  TODO:
    When logging to the binary log, we should log
    tmp_tables and transactional tables as separate statements if we
    are in a transaction;  This is needed to get these tables into the
    cached binary log that is only written on COMMIT.

   The current code only writes DROP statements that only uses temporary
   tables to the cache binary log.  This should be ok on most cases, but
   not all.

 RETURN
   0	ok
   1	Error
   -1	Thread was killed
*/

int mysql_rm_table_part2(Session *session, TableList *tables, bool if_exists,
                         bool drop_temporary, bool dont_log_query)
{
  TableList *table;
  char path[FN_REFLEN];
  uint32_t path_length= 0;
  String wrong_tables;
  int error= 0;
  int non_temp_tables_count= 0;
  bool some_tables_deleted=0, tmp_table_deleted=0, foreign_key_error=0;
  String built_query;

  if (!dont_log_query)
  {
    built_query.set_charset(system_charset_info);
    if (if_exists)
      built_query.append("DROP Table IF EXISTS ");
    else
      built_query.append("DROP Table ");
  }

  pthread_mutex_lock(&LOCK_open); /* Part 2 of rm a table */

  /*
    If we have the table in the definition cache, we don't have to check the
    .frm file to find if the table is a normal table (not view) and what
    engine to use.
  */

  for (table= tables; table; table= table->next_local)
  {
    TableShare *share;
    table->db_type= NULL;
    if ((share= TableShare::getShare(table->db, table->table_name)))
      table->db_type= share->db_type();
  }

  if (!drop_temporary && lock_table_names_exclusively(session, tables))
  {
    pthread_mutex_unlock(&LOCK_open);
    return 1;
  }

  /* Don't give warnings for not found errors, as we already generate notes */
  session->no_warnings_for_error= 1;

  for (table= tables; table; table= table->next_local)
  {
    char *db=table->db;
    StorageEngine *table_type;

    error= session->drop_temporary_table(table);

    switch (error) {
    case  0:
      // removed temporary table
      tmp_table_deleted= 1;
      continue;
    case -1:
      error= 1;
      goto err_with_placeholders;
    default:
      // temporary table not found
      error= 0;
    }

    /*
      If row-based replication is used and the table is not a
      temporary table, we add the table name to the drop statement
      being built.  The string always end in a comma and the comma
      will be chopped off before being written to the binary log.
      */
    if (!dont_log_query)
    {
      non_temp_tables_count++;
      /*
        Don't write the database name if it is the current one (or if
        session->db is NULL).
      */
      built_query.append("`");
      if (session->db == NULL || strcmp(db,session->db) != 0)
      {
        built_query.append(db);
        built_query.append("`.`");
      }

      built_query.append(table->table_name);
      built_query.append("`,");
    }

    table_type= table->db_type;
    if (!drop_temporary)
    {
      Table *locked_table;
      abort_locked_tables(session, db, table->table_name);
      remove_table_from_cache(session, db, table->table_name,
                              RTFC_WAIT_OTHER_THREAD_FLAG |
                              RTFC_CHECK_KILLED_FLAG);
      /*
        If the table was used in lock tables, remember it so that
        unlock_table_names can free it
      */
      if ((locked_table= drop_locked_tables(session, db, table->table_name)))
        table->table= locked_table;

      if (session->killed)
      {
        error= -1;
        goto err_with_placeholders;
      }
      /* remove .frm file and engine files */
      path_length= build_table_filename(path, sizeof(path), db, table->table_name, table->internal_tmp_table);
    }
    if (drop_temporary ||
        ((table_type == NULL
          && (StorageEngine::getTableProto(path, NULL) != EEXIST))))
    {
      // Table was not found on disk and table can't be created from engine
      if (if_exists)
        push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
                            ER_BAD_TABLE_ERROR, ER(ER_BAD_TABLE_ERROR),
                            table->table_name);
      else
        error= 1;
    }
    else
    {
      error= ha_delete_table(session, path, db, table->table_name,
                             !dont_log_query);
      if ((error == ENOENT || error == HA_ERR_NO_SUCH_TABLE) &&
	  if_exists)
      {
	error= 0;
        session->clear_error();
      }
      if (error == HA_ERR_ROW_IS_REFERENCED)
      {
        /* the table is referenced by a foreign key constraint */
        foreign_key_error=1;
      }
      if (error == 0)
      {
          some_tables_deleted=1;
      }
    }
    if (error)
    {
      if (wrong_tables.length())
        wrong_tables.append(',');
      wrong_tables.append(String(table->table_name,system_charset_info));
    }
  }
  /*
    It's safe to unlock LOCK_open: we have an exclusive lock
    on the table name.
  */
  pthread_mutex_unlock(&LOCK_open);
  error= 0;
  if (wrong_tables.length())
  {
    if (!foreign_key_error)
      my_printf_error(ER_BAD_TABLE_ERROR, ER(ER_BAD_TABLE_ERROR), MYF(0),
                      wrong_tables.c_ptr());
    else
    {
      my_message(ER_ROW_IS_REFERENCED, ER(ER_ROW_IS_REFERENCED), MYF(0));
    }
    error= 1;
  }

  if (some_tables_deleted || tmp_table_deleted || !error)
  {
    if (!dont_log_query)
    {
      if ((non_temp_tables_count > 0 && !tmp_table_deleted))
      {
        /*
          In this case, we are either using statement-based
          replication or using row-based replication but have only
          deleted one or more non-temporary tables (and no temporary
          tables).  In this case, we can write the original query into
          the binary log.
         */
        write_bin_log(session, !error, session->query, session->query_length);
      }
      else if (non_temp_tables_count > 0 &&
               tmp_table_deleted)
      {
        /*
          In this case we have deleted both temporary and
          non-temporary tables, so:
          - since we have deleted a non-temporary table we have to
            binlog the statement, but
          - since we have deleted a temporary table we cannot binlog
            the statement (since the table has not been created on the
            slave, this might cause the slave to stop).

          Instead, we write a built statement, only containing the
          non-temporary tables, to the binary log
        */
        built_query.chop();                  // Chop of the last comma
        built_query.append(" /* generated by server */");
        write_bin_log(session, !error, built_query.ptr(), built_query.length());
      }
      /*
        The remaining cases are:
        - no tables where deleted and
        - only temporary tables where deleted and row-based
          replication is used.
        In both these cases, nothing should be written to the binary
        log.
      */
    }
  }
  pthread_mutex_lock(&LOCK_open); /* final bit in rm table lock */
err_with_placeholders:
  unlock_table_names(tables, NULL);
  pthread_mutex_unlock(&LOCK_open);
  session->no_warnings_for_error= 0;

  return(error);
}


/*
  Quickly remove a table.

  SYNOPSIS
    quick_rm_table()
      base                      The StorageEngine handle.
      db                        The database name.
      table_name                The table name.
      is_tmp                    If the table is temp.

  RETURN
    0           OK
    != 0        Error
*/

bool quick_rm_table(StorageEngine *, const char *db,
                    const char *table_name, bool is_tmp)
{
  char path[FN_REFLEN];
  bool error= 0;

  build_table_filename(path, sizeof(path), db, table_name, is_tmp);

  return(ha_delete_table(current_session, path, db, table_name, 0) ||
              error);
}

/*
  Sort keys in the following order:
  - PRIMARY KEY
  - UNIQUE keys where all column are NOT NULL
  - UNIQUE keys that don't contain partial segments
  - Other UNIQUE keys
  - Normal keys
  - Fulltext keys

  This will make checking for duplicated keys faster and ensure that
  PRIMARY keys are prioritized.
*/

static int sort_keys(KEY *a, KEY *b)
{
  ulong a_flags= a->flags, b_flags= b->flags;

  if (a_flags & HA_NOSAME)
  {
    if (!(b_flags & HA_NOSAME))
      return -1;
    if ((a_flags ^ b_flags) & (HA_NULL_PART_KEY))
    {
      /* Sort NOT NULL keys before other keys */
      return (a_flags & (HA_NULL_PART_KEY)) ? 1 : -1;
    }
    if (is_primary_key(a))
      return -1;
    if (is_primary_key(b))
      return 1;
    /* Sort keys don't containing partial segments before others */
    if ((a_flags ^ b_flags) & HA_KEY_HAS_PART_KEY_SEG)
      return (a_flags & HA_KEY_HAS_PART_KEY_SEG) ? 1 : -1;
  }
  else if (b_flags & HA_NOSAME)
    return 1;					// Prefer b

  /*
    Prefer original key order.	usable_key_parts contains here
    the original key position.
  */
  return ((a->usable_key_parts < b->usable_key_parts) ? -1 :
          (a->usable_key_parts > b->usable_key_parts) ? 1 :
          0);
}

/*
  Check TYPELIB (set or enum) for duplicates

  SYNOPSIS
    check_duplicates_in_interval()
    set_or_name   "SET" or "ENUM" string for warning message
    name	  name of the checked column
    typelib	  list of values for the column
    dup_val_count  returns count of duplicate elements

  DESCRIPTION
    This function prints an warning for each value in list
    which has some duplicates on its right

  RETURN VALUES
    0             ok
    1             Error
*/

static bool check_duplicates_in_interval(const char *set_or_name,
                                         const char *name, TYPELIB *typelib,
                                         const CHARSET_INFO * const cs,
                                         unsigned int *dup_val_count)
{
  TYPELIB tmp= *typelib;
  const char **cur_value= typelib->type_names;
  unsigned int *cur_length= typelib->type_lengths;
  *dup_val_count= 0;

  for ( ; tmp.count > 1; cur_value++, cur_length++)
  {
    tmp.type_names++;
    tmp.type_lengths++;
    tmp.count--;
    if (find_type2(&tmp, (const char*)*cur_value, *cur_length, cs))
    {
      my_error(ER_DUPLICATED_VALUE_IN_TYPE, MYF(0),
               name,*cur_value,set_or_name);
      return 1;
    }
  }
  return 0;
}


/*
  Check TYPELIB (set or enum) max and total lengths

  SYNOPSIS
    calculate_interval_lengths()
    cs            charset+collation pair of the interval
    typelib       list of values for the column
    max_length    length of the longest item
    tot_length    sum of the item lengths

  DESCRIPTION
    After this function call:
    - ENUM uses max_length
    - SET uses tot_length.

  RETURN VALUES
    void
*/
static void calculate_interval_lengths(const CHARSET_INFO * const cs,
                                       TYPELIB *interval,
                                       uint32_t *max_length,
                                       uint32_t *tot_length)
{
  const char **pos;
  uint32_t *len;
  *max_length= *tot_length= 0;
  for (pos= interval->type_names, len= interval->type_lengths;
       *pos ; pos++, len++)
  {
    uint32_t length= cs->cset->numchars(cs, *pos, *pos + *len);
    *tot_length+= length;
    set_if_bigger(*max_length, (uint32_t)length);
  }
}


/*
  Prepare a create_table instance for packing

  SYNOPSIS
    prepare_create_field()
    sql_field     field to prepare for packing
    blob_columns  count for BLOBs
    timestamps    count for timestamps
    table_flags   table flags

  DESCRIPTION
    This function prepares a CreateField instance.
    Fields such as pack_flag are valid after this call.

  RETURN VALUES
   0	ok
   1	Error
*/

int prepare_create_field(CreateField *sql_field,
                         uint32_t *blob_columns,
                         int *timestamps, int *timestamps_with_niladic,
                         int64_t )
{
  unsigned int dup_val_count;

  /*
    This code came from mysql_prepare_create_table.
    Indent preserved to make patching easier
  */
  assert(sql_field->charset);

  switch (sql_field->sql_type) {
  case DRIZZLE_TYPE_BLOB:
    sql_field->pack_flag=FIELDFLAG_BLOB |
      pack_length_to_packflag(sql_field->pack_length -
                              portable_sizeof_char_ptr);
    if (sql_field->charset->state & MY_CS_BINSORT)
      sql_field->pack_flag|=FIELDFLAG_BINARY;
    sql_field->length=8;			// Unireg field length
    (*blob_columns)++;
    break;
  case DRIZZLE_TYPE_VARCHAR:
    sql_field->pack_flag=0;
    if (sql_field->charset->state & MY_CS_BINSORT)
      sql_field->pack_flag|=FIELDFLAG_BINARY;
    break;
  case DRIZZLE_TYPE_ENUM:
    sql_field->pack_flag=pack_length_to_packflag(sql_field->pack_length) |
      FIELDFLAG_INTERVAL;
    if (sql_field->charset->state & MY_CS_BINSORT)
      sql_field->pack_flag|=FIELDFLAG_BINARY;
    if (check_duplicates_in_interval("ENUM",sql_field->field_name,
                                 sql_field->interval,
                                     sql_field->charset, &dup_val_count))
      return 1;
    break;
  case DRIZZLE_TYPE_DATE:  // Rest of string types
  case DRIZZLE_TYPE_DATETIME:
  case DRIZZLE_TYPE_NULL:
    sql_field->pack_flag=f_settype((uint32_t) sql_field->sql_type);
    break;
  case DRIZZLE_TYPE_NEWDECIMAL:
    sql_field->pack_flag=(FIELDFLAG_NUMBER |
                          (sql_field->flags & UNSIGNED_FLAG ? 0 :
                           FIELDFLAG_DECIMAL) |
                          (sql_field->flags & DECIMAL_FLAG ?  FIELDFLAG_DECIMAL_POSITION : 0) |
                          (sql_field->decimals << FIELDFLAG_DEC_SHIFT));
    break;
  case DRIZZLE_TYPE_TIMESTAMP:
    /* We should replace old TIMESTAMP fields with their newer analogs */
    if (sql_field->unireg_check == Field::TIMESTAMP_OLD_FIELD)
    {
      if (!*timestamps)
      {
        sql_field->unireg_check= Field::TIMESTAMP_DNUN_FIELD;
        (*timestamps_with_niladic)++;
      }
      else
        sql_field->unireg_check= Field::NONE;
    }
    else if (sql_field->unireg_check != Field::NONE)
      (*timestamps_with_niladic)++;

    (*timestamps)++;
    /* fall-through */
  default:
    sql_field->pack_flag=(FIELDFLAG_NUMBER |
                          (sql_field->flags & UNSIGNED_FLAG ? 0 :
                           FIELDFLAG_DECIMAL) |
                          f_settype((uint32_t) sql_field->sql_type) |
                          (sql_field->decimals << FIELDFLAG_DEC_SHIFT));
    break;
  }
  if (!(sql_field->flags & NOT_NULL_FLAG))
    sql_field->pack_flag|= FIELDFLAG_MAYBE_NULL;
  if (sql_field->flags & NO_DEFAULT_VALUE_FLAG)
    sql_field->pack_flag|= FIELDFLAG_NO_DEFAULT;
  return 0;
}

/*
  Preparation for table creation

  SYNOPSIS
    mysql_prepare_create_table()
      session                       Thread object.
      create_info               Create information (like MAX_ROWS).
      alter_info                List of columns and indexes to create
      tmp_table                 If a temporary table is to be created.
      db_options          INOUT Table options (like HA_OPTION_PACK_RECORD).
      file                      The handler for the new table.
      key_info_buffer     OUT   An array of KEY structs for the indexes.
      key_count           OUT   The number of elements in the array.
      select_field_count        The number of fields coming from a select table.

  DESCRIPTION
    Prepares the table and key structures for table creation.

  NOTES
    sets create_info->varchar if the table has a varchar

  RETURN VALUES
    false    OK
    true     error
*/

static int
mysql_prepare_create_table(Session *session, HA_CREATE_INFO *create_info,
                           Alter_info *alter_info,
                           bool tmp_table,
                               uint32_t *db_options,
                               handler *file, KEY **key_info_buffer,
                               uint32_t *key_count, int select_field_count)
{
  const char	*key_name;
  CreateField	*sql_field,*dup_field;
  uint		field,null_fields,blob_columns,max_key_length;
  ulong		record_offset= 0;
  KEY		*key_info;
  KEY_PART_INFO *key_part_info;
  int		timestamps= 0, timestamps_with_niladic= 0;
  int		field_no,dup_no;
  int		select_field_pos,auto_increment=0;
  List_iterator<CreateField> it(alter_info->create_list);
  List_iterator<CreateField> it2(alter_info->create_list);
  uint32_t total_uneven_bit_length= 0;

  select_field_pos= alter_info->create_list.elements - select_field_count;
  null_fields=blob_columns=0;
  create_info->varchar= 0;
  max_key_length= file->max_key_length();

  for (field_no=0; (sql_field=it++) ; field_no++)
  {
    const CHARSET_INFO *save_cs;

    /*
      Initialize length from its original value (number of characters),
      which was set in the parser. This is necessary if we're
      executing a prepared statement for the second time.
    */
    sql_field->length= sql_field->char_length;
    if (!sql_field->charset)
      sql_field->charset= create_info->default_table_charset;
    /*
      table_charset is set in ALTER Table if we want change character set
      for all varchar/char columns.
      But the table charset must not affect the BLOB fields, so don't
      allow to change my_charset_bin to somethig else.
    */
    if (create_info->table_charset && sql_field->charset != &my_charset_bin)
      sql_field->charset= create_info->table_charset;

    save_cs= sql_field->charset;
    if ((sql_field->flags & BINCMP_FLAG) &&
        !(sql_field->charset= get_charset_by_csname(sql_field->charset->csname, MY_CS_BINSORT)))
    {
      char tmp[64];
      char *tmp_pos= tmp;
      strncpy(tmp_pos, save_cs->csname, sizeof(tmp)-4);
      tmp_pos+= strlen(tmp);
      strncpy(tmp_pos, STRING_WITH_LEN("_bin"));
      my_error(ER_UNKNOWN_COLLATION, MYF(0), tmp);
      return(true);
    }

    /*
      Convert the default value from client character
      set into the column character set if necessary.
    */
    if (sql_field->def &&
        save_cs != sql_field->def->collation.collation &&
        (sql_field->sql_type == DRIZZLE_TYPE_ENUM))
    {
      /*
        Starting from 5.1 we work here with a copy of CreateField
        created by the caller, not with the instance that was
        originally created during parsing. It's OK to create
        a temporary item and initialize with it a member of the
        copy -- this item will be thrown away along with the copy
        at the end of execution, and thus not introduce a dangling
        pointer in the parsed tree of a prepared statement or a
        stored procedure statement.
      */
      sql_field->def= sql_field->def->safe_charset_converter(save_cs);

      if (sql_field->def == NULL)
      {
        /* Could not convert */
        my_error(ER_INVALID_DEFAULT, MYF(0), sql_field->field_name);
        return(true);
      }
    }

    if (sql_field->sql_type == DRIZZLE_TYPE_ENUM)
    {
      uint32_t dummy;
      const CHARSET_INFO * const cs= sql_field->charset;
      TYPELIB *interval= sql_field->interval;

      /*
        Create typelib from interval_list, and if necessary
        convert strings from client character set to the
        column character set.
      */
      if (!interval)
      {
        /*
          Create the typelib in runtime memory - we will free the
          occupied memory at the same time when we free this
          sql_field -- at the end of execution.
        */
        interval= sql_field->interval= typelib(session->mem_root,
                                               sql_field->interval_list);

        List_iterator<String> int_it(sql_field->interval_list);
        String conv, *tmp;
        char comma_buf[4];
        int comma_length= cs->cset->wc_mb(cs, ',', (unsigned char*) comma_buf,
                                          (unsigned char*) comma_buf +
                                          sizeof(comma_buf));
        assert(comma_length > 0);

        for (uint32_t i= 0; (tmp= int_it++); i++)
        {
          uint32_t lengthsp;
          if (String::needs_conversion(tmp->length(), tmp->charset(),
                                       cs, &dummy))
          {
            uint32_t cnv_errs;
            conv.copy(tmp->ptr(), tmp->length(), tmp->charset(), cs, &cnv_errs);
            interval->type_names[i]= strmake_root(session->mem_root, conv.ptr(),
                                                  conv.length());
            interval->type_lengths[i]= conv.length();
          }

          // Strip trailing spaces.
          lengthsp= cs->cset->lengthsp(cs, interval->type_names[i],
                                       interval->type_lengths[i]);
          interval->type_lengths[i]= lengthsp;
          ((unsigned char *)interval->type_names[i])[lengthsp]= '\0';
        }
        sql_field->interval_list.empty(); // Don't need interval_list anymore
      }

      /* DRIZZLE_TYPE_ENUM */
      {
        uint32_t field_length;
        assert(sql_field->sql_type == DRIZZLE_TYPE_ENUM);
        if (sql_field->def != NULL)
        {
          String str, *def= sql_field->def->val_str(&str);
          if (def == NULL) /* SQL "NULL" maps to NULL */
          {
            if ((sql_field->flags & NOT_NULL_FLAG) != 0)
            {
              my_error(ER_INVALID_DEFAULT, MYF(0), sql_field->field_name);
              return(true);
            }

            /* else, the defaults yield the correct length for NULLs. */
          }
          else /* not NULL */
          {
            def->length(cs->cset->lengthsp(cs, def->ptr(), def->length()));
            if (find_type2(interval, def->ptr(), def->length(), cs) == 0) /* not found */
            {
              my_error(ER_INVALID_DEFAULT, MYF(0), sql_field->field_name);
              return(true);
            }
          }
        }
        calculate_interval_lengths(cs, interval, &field_length, &dummy);
        sql_field->length= field_length;
      }
      set_if_smaller(sql_field->length, (uint32_t)MAX_FIELD_WIDTH-1);
    }

    sql_field->create_length_to_internal_length();
    if (prepare_blob_field(session, sql_field))
      return(true);

    if (!(sql_field->flags & NOT_NULL_FLAG))
      null_fields++;

    if (check_column_name(sql_field->field_name))
    {
      my_error(ER_WRONG_COLUMN_NAME, MYF(0), sql_field->field_name);
      return(true);
    }

    /* Check if we have used the same field name before */
    for (dup_no=0; (dup_field=it2++) != sql_field; dup_no++)
    {
      if (my_strcasecmp(system_charset_info,
                        sql_field->field_name,
                        dup_field->field_name) == 0)
      {
	/*
	  If this was a CREATE ... SELECT statement, accept a field
	  redefinition if we are changing a field in the SELECT part
	*/
	if (field_no < select_field_pos || dup_no >= select_field_pos)
	{
	  my_error(ER_DUP_FIELDNAME, MYF(0), sql_field->field_name);
	  return(true);
	}
	else
	{
	  /* Field redefined */
	  sql_field->def=		dup_field->def;
	  sql_field->sql_type=		dup_field->sql_type;
	  sql_field->charset=		(dup_field->charset ?
					 dup_field->charset :
					 create_info->default_table_charset);
	  sql_field->length=		dup_field->char_length;
          sql_field->pack_length=	dup_field->pack_length;
          sql_field->key_length=	dup_field->key_length;
	  sql_field->decimals=		dup_field->decimals;
	  sql_field->create_length_to_internal_length();
	  sql_field->unireg_check=	dup_field->unireg_check;
          /*
            We're making one field from two, the result field will have
            dup_field->flags as flags. If we've incremented null_fields
            because of sql_field->flags, decrement it back.
          */
          if (!(sql_field->flags & NOT_NULL_FLAG))
            null_fields--;
	  sql_field->flags=		dup_field->flags;
          sql_field->interval=          dup_field->interval;
	  it2.remove();			// Remove first (create) definition
	  select_field_pos--;
	  break;
	}
      }
    }
    /* Don't pack rows in old tables if the user has requested this */
    if ((sql_field->flags & BLOB_FLAG) ||
	(sql_field->sql_type == DRIZZLE_TYPE_VARCHAR && create_info->row_type != ROW_TYPE_FIXED))
      (*db_options)|= HA_OPTION_PACK_RECORD;
    it2.rewind();
  }

  /* record_offset will be increased with 'length-of-null-bits' later */
  record_offset= 0;
  null_fields+= total_uneven_bit_length;

  it.rewind();
  while ((sql_field=it++))
  {
    assert(sql_field->charset != 0);

    if (prepare_create_field(sql_field, &blob_columns,
			     &timestamps, &timestamps_with_niladic,
			     file->ha_table_flags()))
      return(true);
    if (sql_field->sql_type == DRIZZLE_TYPE_VARCHAR)
      create_info->varchar= true;
    sql_field->offset= record_offset;
    if (MTYP_TYPENR(sql_field->unireg_check) == Field::NEXT_NUMBER)
      auto_increment++;
  }
  if (timestamps_with_niladic > 1)
  {
    my_message(ER_TOO_MUCH_AUTO_TIMESTAMP_COLS,
               ER(ER_TOO_MUCH_AUTO_TIMESTAMP_COLS), MYF(0));
    return(true);
  }
  if (auto_increment > 1)
  {
    my_message(ER_WRONG_AUTO_KEY, ER(ER_WRONG_AUTO_KEY), MYF(0));
    return(true);
  }
  if (auto_increment &&
      (file->ha_table_flags() & HA_NO_AUTO_INCREMENT))
  {
    my_message(ER_TABLE_CANT_HANDLE_AUTO_INCREMENT,
               ER(ER_TABLE_CANT_HANDLE_AUTO_INCREMENT), MYF(0));
    return(true);
  }

  if (blob_columns && (file->ha_table_flags() & HA_NO_BLOBS))
  {
    my_message(ER_TABLE_CANT_HANDLE_BLOB, ER(ER_TABLE_CANT_HANDLE_BLOB),
               MYF(0));
    return(true);
  }

  /* Create keys */

  List_iterator<Key> key_iterator(alter_info->key_list);
  List_iterator<Key> key_iterator2(alter_info->key_list);
  uint32_t key_parts=0, fk_key_count=0;
  bool primary_key=0,unique_key=0;
  Key *key, *key2;
  uint32_t tmp, key_number;
  /* special marker for keys to be ignored */
  static char ignore_key[1];

  /* Calculate number of key segements */
  *key_count= 0;

  while ((key=key_iterator++))
  {
    if (key->type == Key::FOREIGN_KEY)
    {
      fk_key_count++;
      if (((Foreign_key *)key)->validate(alter_info->create_list))
        return true;
      Foreign_key *fk_key= (Foreign_key*) key;
      if (fk_key->ref_columns.elements &&
	  fk_key->ref_columns.elements != fk_key->columns.elements)
      {
        my_error(ER_WRONG_FK_DEF, MYF(0),
                 (fk_key->name.str ? fk_key->name.str :
                                     "foreign key without name"),
                 ER(ER_KEY_REF_DO_NOT_MATCH_TABLE_REF));
	return(true);
      }
      continue;
    }
    (*key_count)++;
    tmp=file->max_key_parts();
    if (key->columns.elements > tmp)
    {
      my_error(ER_TOO_MANY_KEY_PARTS,MYF(0),tmp);
      return(true);
    }
    if (check_identifier_name(&key->name, ER_TOO_LONG_IDENT))
      return(true);
    key_iterator2.rewind ();
    if (key->type != Key::FOREIGN_KEY)
    {
      while ((key2 = key_iterator2++) != key)
      {
	/*
          foreign_key_prefix(key, key2) returns 0 if key or key2, or both, is
          'generated', and a generated key is a prefix of the other key.
          Then we do not need the generated shorter key.
        */
        if ((key2->type != Key::FOREIGN_KEY &&
             key2->name.str != ignore_key &&
             !foreign_key_prefix(key, key2)))
        {
          /* TODO: issue warning message */
          /* mark that the generated key should be ignored */
          if (!key2->generated ||
              (key->generated && key->columns.elements <
               key2->columns.elements))
            key->name.str= ignore_key;
          else
          {
            key2->name.str= ignore_key;
            key_parts-= key2->columns.elements;
            (*key_count)--;
          }
          break;
        }
      }
    }
    if (key->name.str != ignore_key)
      key_parts+=key->columns.elements;
    else
      (*key_count)--;
    if (key->name.str && !tmp_table && (key->type != Key::PRIMARY) &&
        is_primary_key_name(key->name.str))
    {
      my_error(ER_WRONG_NAME_FOR_INDEX, MYF(0), key->name.str);
      return(true);
    }
  }
  tmp=file->max_keys();
  if (*key_count > tmp)
  {
    my_error(ER_TOO_MANY_KEYS,MYF(0),tmp);
    return(true);
  }

  (*key_info_buffer)= key_info= (KEY*) sql_calloc(sizeof(KEY) * (*key_count));
  key_part_info=(KEY_PART_INFO*) sql_calloc(sizeof(KEY_PART_INFO)*key_parts);
  if (!*key_info_buffer || ! key_part_info)
    return(true);				// Out of memory

  key_iterator.rewind();
  key_number=0;
  for (; (key=key_iterator++) ; key_number++)
  {
    uint32_t key_length=0;
    Key_part_spec *column;

    if (key->name.str == ignore_key)
    {
      /* ignore redundant keys */
      do
	key=key_iterator++;
      while (key && key->name.str == ignore_key);
      if (!key)
	break;
    }

    switch (key->type) {
    case Key::MULTIPLE:
	key_info->flags= 0;
	break;
    case Key::FOREIGN_KEY:
      key_number--;				// Skip this key
      continue;
    default:
      key_info->flags = HA_NOSAME;
      break;
    }
    if (key->generated)
      key_info->flags|= HA_GENERATED_KEY;

    key_info->key_parts=(uint8_t) key->columns.elements;
    key_info->key_part=key_part_info;
    key_info->usable_key_parts= key_number;
    key_info->algorithm= key->key_create_info.algorithm;

    /* Take block size from key part or table part */
    /*
      TODO: Add warning if block size changes. We can't do it here, as
      this may depend on the size of the key
    */
    key_info->block_size= (key->key_create_info.block_size ?
                           key->key_create_info.block_size :
                           create_info->key_block_size);

    if (key_info->block_size)
      key_info->flags|= HA_USES_BLOCK_SIZE;

    uint32_t tmp_len= system_charset_info->cset->charpos(system_charset_info,
                                           key->key_create_info.comment.str,
                                           key->key_create_info.comment.str +
                                           key->key_create_info.comment.length,
                                           INDEX_COMMENT_MAXLEN);

    if (tmp_len < key->key_create_info.comment.length)
    {
      my_error(ER_WRONG_STRING_LENGTH, MYF(0),
               key->key_create_info.comment.str,"INDEX COMMENT",
               (uint32_t) INDEX_COMMENT_MAXLEN);
      return -1;
    }

    key_info->comment.length= key->key_create_info.comment.length;
    if (key_info->comment.length > 0)
    {
      key_info->flags|= HA_USES_COMMENT;
      key_info->comment.str= key->key_create_info.comment.str;
    }

    List_iterator<Key_part_spec> cols(key->columns), cols2(key->columns);
    for (uint32_t column_nr=0 ; (column=cols++) ; column_nr++)
    {
      uint32_t length;
      Key_part_spec *dup_column;

      it.rewind();
      field=0;
      while ((sql_field=it++) &&
	     my_strcasecmp(system_charset_info,
			   column->field_name.str,
			   sql_field->field_name))
	field++;
      if (!sql_field)
      {
	my_error(ER_KEY_COLUMN_DOES_NOT_EXITS, MYF(0), column->field_name.str);
	return(true);
      }
      while ((dup_column= cols2++) != column)
      {
        if (!my_strcasecmp(system_charset_info,
	     	           column->field_name.str, dup_column->field_name.str))
	{
	  my_printf_error(ER_DUP_FIELDNAME,
			  ER(ER_DUP_FIELDNAME),MYF(0),
			  column->field_name.str);
	  return(true);
	}
      }
      cols2.rewind();
      {
	column->length*= sql_field->charset->mbmaxlen;

	if (f_is_blob(sql_field->pack_flag))
	{
	  if (!(file->ha_table_flags() & HA_CAN_INDEX_BLOBS))
	  {
	    my_error(ER_BLOB_USED_AS_KEY, MYF(0), column->field_name.str);
	    return(true);
	  }
	  if (!column->length)
	  {
	    my_error(ER_BLOB_KEY_WITHOUT_LENGTH, MYF(0), column->field_name.str);
	    return(true);
	  }
	}
	if (!(sql_field->flags & NOT_NULL_FLAG))
	{
	  if (key->type == Key::PRIMARY)
	  {
	    /* Implicitly set primary key fields to NOT NULL for ISO conf. */
	    sql_field->flags|= NOT_NULL_FLAG;
	    sql_field->pack_flag&= ~FIELDFLAG_MAYBE_NULL;
            null_fields--;
	  }
	  else
          {
            key_info->flags|= HA_NULL_PART_KEY;
            if (!(file->ha_table_flags() & HA_NULL_IN_KEY))
            {
              my_error(ER_NULL_COLUMN_IN_INDEX, MYF(0), column->field_name.str);
              return(true);
            }
          }
	}
	if (MTYP_TYPENR(sql_field->unireg_check) == Field::NEXT_NUMBER)
	{
	  if (column_nr == 0 || (file->ha_table_flags() & HA_AUTO_PART_KEY))
	    auto_increment--;			// Field is used
	}
      }

      key_part_info->fieldnr= field;
      key_part_info->offset=  (uint16_t) sql_field->offset;
      key_part_info->key_type=sql_field->pack_flag;
      length= sql_field->key_length;

      if (column->length)
      {
	if (f_is_blob(sql_field->pack_flag))
	{
	  if ((length=column->length) > max_key_length ||
	      length > file->max_key_part_length())
	  {
	    length= min(max_key_length, file->max_key_part_length());
	    if (key->type == Key::MULTIPLE)
	    {
	      /* not a critical problem */
	      char warn_buff[DRIZZLE_ERRMSG_SIZE];
	      snprintf(warn_buff, sizeof(warn_buff), ER(ER_TOO_LONG_KEY),
                       length);
	      push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
			   ER_TOO_LONG_KEY, warn_buff);
              /* Align key length to multibyte char boundary */
              length-= length % sql_field->charset->mbmaxlen;
	    }
	    else
	    {
	      my_error(ER_TOO_LONG_KEY,MYF(0),length);
	      return(true);
	    }
	  }
	}
	else if ((column->length > length ||
                   !Field::type_can_have_key_part (sql_field->sql_type) ||
		   ((f_is_packed(sql_field->pack_flag) ||
		     ((file->ha_table_flags() & HA_NO_PREFIX_CHAR_KEYS) &&
		      (key_info->flags & HA_NOSAME))) &&
		    column->length != length)))
	{
	  my_message(ER_WRONG_SUB_KEY, ER(ER_WRONG_SUB_KEY), MYF(0));
	  return(true);
	}
	else if (!(file->ha_table_flags() & HA_NO_PREFIX_CHAR_KEYS))
	  length=column->length;
      }
      else if (length == 0)
      {
	my_error(ER_WRONG_KEY_COLUMN, MYF(0), column->field_name.str);
	  return(true);
      }
      if (length > file->max_key_part_length())
      {
        length= file->max_key_part_length();
	if (key->type == Key::MULTIPLE)
	{
	  /* not a critical problem */
	  char warn_buff[DRIZZLE_ERRMSG_SIZE];
	  snprintf(warn_buff, sizeof(warn_buff), ER(ER_TOO_LONG_KEY),
                   length);
	  push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
		       ER_TOO_LONG_KEY, warn_buff);
          /* Align key length to multibyte char boundary */
          length-= length % sql_field->charset->mbmaxlen;
	}
	else
	{
	  my_error(ER_TOO_LONG_KEY,MYF(0),length);
	  return(true);
	}
      }
      key_part_info->length=(uint16_t) length;
      /* Use packed keys for long strings on the first column */
      if (!((*db_options) & HA_OPTION_NO_PACK_KEYS) &&
	  (length >= KEY_DEFAULT_PACK_LENGTH &&
	   (sql_field->sql_type == DRIZZLE_TYPE_VARCHAR ||
	    sql_field->pack_flag & FIELDFLAG_BLOB)))
      {
	if ((column_nr == 0 && (sql_field->pack_flag & FIELDFLAG_BLOB)) ||
            sql_field->sql_type == DRIZZLE_TYPE_VARCHAR)
	  key_info->flags|= HA_BINARY_PACK_KEY | HA_VAR_LENGTH_KEY;
	else
	  key_info->flags|= HA_PACK_KEY;
      }
      /* Check if the key segment is partial, set the key flag accordingly */
      if (length != sql_field->key_length)
        key_info->flags|= HA_KEY_HAS_PART_KEY_SEG;

      key_length+=length;
      key_part_info++;

      /* Create the key name based on the first column (if not given) */
      if (column_nr == 0)
      {
	if (key->type == Key::PRIMARY)
	{
	  if (primary_key)
	  {
	    my_message(ER_MULTIPLE_PRI_KEY, ER(ER_MULTIPLE_PRI_KEY),
                       MYF(0));
	    return(true);
	  }
          static const char pkey_name[]= "PRIMARY";
	  key_name=pkey_name;
	  primary_key=1;
	}
	else if (!(key_name= key->name.str))
	  key_name=make_unique_key_name(sql_field->field_name,
					*key_info_buffer, key_info);
	if (check_if_keyname_exists(key_name, *key_info_buffer, key_info))
	{
	  my_error(ER_DUP_KEYNAME, MYF(0), key_name);
	  return(true);
	}
	key_info->name=(char*) key_name;
      }
    }
    if (!key_info->name || check_column_name(key_info->name))
    {
      my_error(ER_WRONG_NAME_FOR_INDEX, MYF(0), key_info->name);
      return(true);
    }
    if (!(key_info->flags & HA_NULL_PART_KEY))
      unique_key=1;
    key_info->key_length=(uint16_t) key_length;
    if (key_length > max_key_length)
    {
      my_error(ER_TOO_LONG_KEY,MYF(0),max_key_length);
      return(true);
    }
    key_info++;
  }
  if (!unique_key && !primary_key &&
      (file->ha_table_flags() & HA_REQUIRE_PRIMARY_KEY))
  {
    my_message(ER_REQUIRES_PRIMARY_KEY, ER(ER_REQUIRES_PRIMARY_KEY), MYF(0));
    return(true);
  }
  if (auto_increment > 0)
  {
    my_message(ER_WRONG_AUTO_KEY, ER(ER_WRONG_AUTO_KEY), MYF(0));
    return(true);
  }
  /* Sort keys in optimized order */
  my_qsort((unsigned char*) *key_info_buffer, *key_count, sizeof(KEY),
	   (qsort_cmp) sort_keys);
  create_info->null_bits= null_fields;

  /* Check fields. */
  it.rewind();
  while ((sql_field=it++))
  {
    Field::utype type= (Field::utype) MTYP_TYPENR(sql_field->unireg_check);

    if (session->variables.sql_mode & MODE_NO_ZERO_DATE &&
        !sql_field->def &&
        sql_field->sql_type == DRIZZLE_TYPE_TIMESTAMP &&
        (sql_field->flags & NOT_NULL_FLAG) &&
        (type == Field::NONE || type == Field::TIMESTAMP_UN_FIELD))
    {
      /*
        An error should be reported if:
          - NO_ZERO_DATE SQL mode is active;
          - there is no explicit DEFAULT clause (default column value);
          - this is a TIMESTAMP column;
          - the column is not NULL;
          - this is not the DEFAULT CURRENT_TIMESTAMP column.

        In other words, an error should be reported if
          - NO_ZERO_DATE SQL mode is active;
          - the column definition is equivalent to
            'column_name TIMESTAMP DEFAULT 0'.
      */

      my_error(ER_INVALID_DEFAULT, MYF(0), sql_field->field_name);
      return(true);
    }
  }

  return(false);
}

/*
  Extend long VARCHAR fields to blob & prepare field if it's a blob

  SYNOPSIS
    prepare_blob_field()
    sql_field		Field to check

  RETURN
    0	ok
    1	Error (sql_field can't be converted to blob)
        In this case the error is given
*/

static bool prepare_blob_field(Session *,
                               CreateField *sql_field)
{

  if (sql_field->length > MAX_FIELD_VARCHARLENGTH &&
      !(sql_field->flags & BLOB_FLAG))
  {
    my_error(ER_TOO_BIG_FIELDLENGTH, MYF(0), sql_field->field_name,
             MAX_FIELD_VARCHARLENGTH / sql_field->charset->mbmaxlen);
    return 1;
  }

  if ((sql_field->flags & BLOB_FLAG) && sql_field->length)
  {
    if (sql_field->sql_type == DRIZZLE_TYPE_BLOB)
    {
      /* The user has given a length to the blob column */
      sql_field->pack_length= calc_pack_length(sql_field->sql_type, 0);
    }
    sql_field->length= 0;
  }
  return 0;
}


/*
  Ignore the name of this function... it locks :(

  Create a table

  SYNOPSIS
    mysql_create_table_no_lock()
    session			Thread object
    db			Database
    table_name		Table name
    create_info	        Create information (like MAX_ROWS)
    fields		List of fields to create
    keys		List of keys to create
    internal_tmp_table  Set to 1 if this is an internal temporary table
			(From ALTER Table)
    select_field_count

  DESCRIPTION
    If one creates a temporary table, this is automatically opened

    Note that this function assumes that caller already have taken
    name-lock on table being created or used some other way to ensure
    that concurrent operations won't intervene. mysql_create_table()
    is a wrapper that can be used for this.

  RETURN VALUES
    false OK
    true  error
*/

bool mysql_create_table_no_lock(Session *session,
                                const char *db, const char *table_name,
                                HA_CREATE_INFO *create_info,
				drizzled::message::Table *table_proto,
                                Alter_info *alter_info,
                                bool internal_tmp_table,
                                uint32_t select_field_count)
{
  char		path[FN_REFLEN];
  uint32_t          path_length;
  uint		db_options, key_count;
  KEY		*key_info_buffer;
  handler	*file;
  bool		error= true;
  /* Check for duplicate fields and check type of table to create */
  if (!alter_info->create_list.elements)
  {
    my_message(ER_TABLE_MUST_HAVE_COLUMNS, ER(ER_TABLE_MUST_HAVE_COLUMNS),
               MYF(0));
    return true;
  }
  assert(strcmp(table_name,table_proto->name().c_str())==0);
  if (check_engine(session, table_name, create_info))
    return true;
  db_options= create_info->table_options;
  if (create_info->row_type == ROW_TYPE_DYNAMIC)
    db_options|=HA_OPTION_PACK_RECORD;
  if (!(file= get_new_handler((TableShare*) 0, session->mem_root,
                              create_info->db_type)))
  {
    my_error(ER_OUTOFMEMORY, MYF(0), sizeof(handler));
    return true;
  }

  set_table_default_charset(create_info, (char*) db);

  if (mysql_prepare_create_table(session, create_info, alter_info,
                                 internal_tmp_table,
                                 &db_options, file,
                                 &key_info_buffer, &key_count,
                                 select_field_count))
    goto err;

      /* Check if table exists */
  if (create_info->options & HA_LEX_CREATE_TMP_TABLE)
  {
    path_length= build_tmptable_filename(session, path, sizeof(path));
    create_info->table_options|=HA_CREATE_DELAY_KEY_WRITE;
  }
  else
  {
 #ifdef FN_DEVCHAR
    /* check if the table name contains FN_DEVCHAR when defined */
    if (strchr(table_name, FN_DEVCHAR))
    {
      my_error(ER_WRONG_TABLE_NAME, MYF(0), table_name);
      return true;
    }
#endif
    path_length= build_table_filename(path, sizeof(path), db, table_name, internal_tmp_table);
  }

  /*
   * If the DATA DIRECTORY or INDEX DIRECTORY options are specified in the
   * create table statement, check whether the storage engine supports those
   * options. If not, return an appropriate error.
   */
  if (create_info->data_file_name &&
      ! create_info->db_type->check_flag(HTON_BIT_DATA_DIR))
  {
    my_error(ER_ILLEGAL_HA_CREATE_OPTION, MYF(0),
             create_info->db_type->getName().c_str(), 
             "DATA DIRECTORY");
    goto err;
  }

  if (create_info->index_file_name &&
      ! create_info->db_type->check_flag(HTON_BIT_INDEX_DIR))
  {
    my_error(ER_ILLEGAL_HA_CREATE_OPTION, MYF(0),
             create_info->db_type->getName().c_str(), 
             "INDEX DIRECTORY");
    goto err;
  }

  /* Check if table already exists */
  if ((create_info->options & HA_LEX_CREATE_TMP_TABLE) &&
      session->find_temporary_table(db, table_name))
  {
    if (create_info->options & HA_LEX_CREATE_IF_NOT_EXISTS)
    {
      create_info->table_existed= 1;		// Mark that table existed
      push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
                          ER_TABLE_EXISTS_ERROR, ER(ER_TABLE_EXISTS_ERROR),
                          table_name);
      error= 0;
      goto err;
    }
    my_error(ER_TABLE_EXISTS_ERROR, MYF(0), table_name);
    goto err;
  }

  pthread_mutex_lock(&LOCK_open); /* CREATE TABLE (some confussion on naming, double check) */
  if (!internal_tmp_table && !(create_info->options & HA_LEX_CREATE_TMP_TABLE))
  {
    if (StorageEngine::getTableProto(path, NULL)==EEXIST)
    {
      if (create_info->options & HA_LEX_CREATE_IF_NOT_EXISTS)
      {
        error= false;
        push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
                            ER_TABLE_EXISTS_ERROR, ER(ER_TABLE_EXISTS_ERROR),
                            table_name);
        create_info->table_existed= 1;		// Mark that table existed
      }
      else 
        my_error(ER_TABLE_EXISTS_ERROR,MYF(0),table_name);

      goto unlock_and_end;
    }
    /*
      We don't assert here, but check the result, because the table could be
      in the table definition cache and in the same time the .frm could be
      missing from the disk, in case of manual intervention which deletes
      the .frm file. The user has to use FLUSH TABLES; to clear the cache.
      Then she could create the table. This case is pretty obscure and
      therefore we don't introduce a new error message only for it.
    */
    if (TableShare::getShare(db, table_name))
    {
      my_error(ER_TABLE_EXISTS_ERROR, MYF(0), table_name);
      goto unlock_and_end;
    }
  }

  /*
    Check that table with given name does not already
    exist in any storage engine. In such a case it should
    be discovered and the error ER_TABLE_EXISTS_ERROR be returned
    unless user specified CREATE TABLE IF EXISTS
    The LOCK_open mutex has been locked to make sure no
    one else is attempting to discover the table. Since
    it's not on disk as a frm file, no one could be using it!
  */
  if (!(create_info->options & HA_LEX_CREATE_TMP_TABLE))
  {
    bool create_if_not_exists =
      create_info->options & HA_LEX_CREATE_IF_NOT_EXISTS;

    char table_path[FN_REFLEN];
    uint32_t          table_path_length;

    table_path_length= build_table_filename(table_path, sizeof(table_path),
                                            db, table_name, false);

    int retcode= StorageEngine::getTableProto(table_path, NULL);
    switch (retcode)
    {
      case ENOENT:
        /* Normal case, no table exists. we can go and create it */
        break;
      case EEXIST:
        if (create_if_not_exists)
        {
          error= false;
          push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
                              ER_TABLE_EXISTS_ERROR, ER(ER_TABLE_EXISTS_ERROR),
                              table_name);
          create_info->table_existed= 1;		// Mark that table existed
          goto unlock_and_end;
        }
        my_error(ER_TABLE_EXISTS_ERROR,MYF(0),table_name);
        goto unlock_and_end;
      default:
        my_error(retcode, MYF(0),table_name);
        goto unlock_and_end;
    }
  }

  session->set_proc_info("creating table");
  create_info->table_existed= 0;		// Mark that table is created

#ifdef HAVE_READLINK
  if (test_if_data_home_dir(create_info->data_file_name))
  {
    my_error(ER_WRONG_ARGUMENTS, MYF(0), "DATA DIRECTORY");
    goto unlock_and_end;
  }
  if (test_if_data_home_dir(create_info->index_file_name))
  {
    my_error(ER_WRONG_ARGUMENTS, MYF(0), "INDEX DIRECTORY");
    goto unlock_and_end;
  }

  if (!my_use_symdir)
#endif /* HAVE_READLINK */
  {
    if (create_info->data_file_name)
      push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_WARN, 0,
                   "DATA DIRECTORY option ignored");
    if (create_info->index_file_name)
      push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_WARN, 0,
                   "INDEX DIRECTORY option ignored");
    create_info->data_file_name= create_info->index_file_name= 0;
  }
  create_info->table_options=db_options;

  if (rea_create_table(session, path, db, table_name,
		       table_proto,
                       create_info, alter_info->create_list,
                       key_count, key_info_buffer))
    goto unlock_and_end;

  if (create_info->options & HA_LEX_CREATE_TMP_TABLE)
  {
    /* Open table and put in temporary table list */
    if (!(session->open_temporary_table(path, db, table_name, 1, OTM_OPEN)))
    {
      (void) session->rm_temporary_table(create_info->db_type, path);
      goto unlock_and_end;
    }
  }

  /*
    Don't write statement if:
    - It is an internal temporary table,
    - Row-based logging is used and it we are creating a temporary table, or
    - The binary log is not open.
    Otherwise, the statement shall be binlogged.
   */
  if (!internal_tmp_table &&
      ((!(create_info->options & HA_LEX_CREATE_TMP_TABLE))))
    write_bin_log(session, true, session->query, session->query_length);
  error= false;
unlock_and_end:
  pthread_mutex_unlock(&LOCK_open);

err:
  session->set_proc_info("After create");
  delete file;
  return(error);
}


/*
  Database locking aware wrapper for mysql_create_table_no_lock(),
*/

bool mysql_create_table(Session *session, const char *db, const char *table_name,
                        HA_CREATE_INFO *create_info,
			drizzled::message::Table *table_proto,
                        Alter_info *alter_info,
                        bool internal_tmp_table,
                        uint32_t select_field_count)
{
  Table *name_lock= NULL;
  bool result;

  if (!(create_info->options & HA_LEX_CREATE_TMP_TABLE))
  {
    if (session->lock_table_name_if_not_cached(db, table_name, &name_lock))
    {
      result= true;
      goto unlock;
    }
    if (name_lock == NULL)
    {
      if (create_info->options & HA_LEX_CREATE_IF_NOT_EXISTS)
      {
        push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
                            ER_TABLE_EXISTS_ERROR, ER(ER_TABLE_EXISTS_ERROR),
                            table_name);
        create_info->table_existed= 1;
        result= false;
      }
      else
      {
        my_error(ER_TABLE_EXISTS_ERROR,MYF(0),table_name);
        result= true;
      }
      goto unlock;
    }
  }

  result= mysql_create_table_no_lock(session, db, table_name, create_info,
				     table_proto,
                                     alter_info,
                                     internal_tmp_table,
                                     select_field_count);

unlock:
  if (name_lock)
  {
    pthread_mutex_lock(&LOCK_open); /* Lock for removing name_lock during table create */
    session->unlink_open_table(name_lock);
    pthread_mutex_unlock(&LOCK_open);
  }

  return(result);
}


/*
** Give the key name after the first field with an optional '_#' after
**/

static bool
check_if_keyname_exists(const char *name, KEY *start, KEY *end)
{
  for (KEY *key=start ; key != end ; key++)
    if (!my_strcasecmp(system_charset_info,name,key->name))
      return 1;
  return 0;
}


static char *
make_unique_key_name(const char *field_name,KEY *start,KEY *end)
{
  char buff[MAX_FIELD_NAME],*buff_end;

  if (!check_if_keyname_exists(field_name,start,end) &&
      !is_primary_key_name(field_name))
    return (char*) field_name;			// Use fieldname

  buff_end= strncpy(buff, field_name, sizeof(buff)-4);
  buff_end+= strlen(buff);

  /*
    Only 3 chars + '\0' left, so need to limit to 2 digit
    This is ok as we can't have more than 100 keys anyway
  */
  for (uint32_t i=2 ; i< 100; i++)
  {
    *buff_end= '_';
    int10_to_str(i, buff_end+1, 10);
    if (!check_if_keyname_exists(buff,start,end))
      return sql_strdup(buff);
  }
  return (char*) "not_specified";		// Should never happen
}


/****************************************************************************
** Alter a table definition
****************************************************************************/


/*
  Rename a table.

  SYNOPSIS
    mysql_rename_table()
      base                      The StorageEngine handle.
      old_db                    The old database name.
      old_name                  The old table name.
      new_db                    The new database name.
      new_name                  The new table name.
      flags                     flags for build_table_filename().
                                FN_FROM_IS_TMP old_name is temporary.
                                FN_TO_IS_TMP   new_name is temporary.
                                NO_FRM_RENAME  Don't rename the FRM file
                                but only the table in the storage engine.

  RETURN
    false   OK
    true    Error
*/

bool
mysql_rename_table(StorageEngine *base, const char *old_db,
                   const char *old_name, const char *new_db,
                   const char *new_name, uint32_t flags)
{
  Session *session= current_session;
  char from[FN_REFLEN], to[FN_REFLEN];
  char *from_base= from, *to_base= to;
  int error= 0;

  assert(base);

  build_table_filename(from, sizeof(from), old_db, old_name,
                       flags & FN_FROM_IS_TMP);
  build_table_filename(to, sizeof(to), new_db, new_name,
                       flags & FN_TO_IS_TMP);

  if (!(error=base->renameTable(session, from_base, to_base)))
  {
    if(!(flags & NO_FRM_RENAME)
       && base->check_flag(HTON_BIT_HAS_DATA_DICTIONARY) == 0
       && rename_table_proto_file(from_base, to_base))
    {
      error= my_errno;
      base->renameTable(session, to_base, from_base);
    }
  }

  if (error == HA_ERR_WRONG_COMMAND)
    my_error(ER_NOT_SUPPORTED_YET, MYF(0), "ALTER Table");
  else if (error)
    my_error(ER_ERROR_ON_RENAME, MYF(0), from, to, error);
  return(error != 0);
}


/*
  Force all other threads to stop using the table

  SYNOPSIS
    wait_while_table_is_used()
    session			Thread handler
    table		Table to remove from cache
    function            HA_EXTRA_PREPARE_FOR_DROP if table is to be deleted
                        HA_EXTRA_FORCE_REOPEN if table is not be used
                        HA_EXTRA_PREPARE_FOR_RENAME if table is to be renamed
  NOTES
   When returning, the table will be unusable for other threads until
   the table is closed.

  PREREQUISITES
    Lock on LOCK_open
    Win32 clients must also have a WRITE LOCK on the table !
*/

void wait_while_table_is_used(Session *session, Table *table,
                              enum ha_extra_function function)
{

  safe_mutex_assert_owner(&LOCK_open);

  table->file->extra(function);
  /* Mark all tables that are in use as 'old' */
  mysql_lock_abort(session, table);	/* end threads waiting on lock */

  /* Wait until all there are no other threads that has this table open */
  remove_table_from_cache(session, table->s->db.str,
                          table->s->table_name.str,
                          RTFC_WAIT_OTHER_THREAD_FLAG);
}

/*
  Close a cached table

  SYNOPSIS
    close_cached_table()
    session			Thread handler
    table		Table to remove from cache

  NOTES
    Function ends by signaling threads waiting for the table to try to
    reopen the table.

  PREREQUISITES
    Lock on LOCK_open
    Win32 clients must also have a WRITE LOCK on the table !
*/

void Session::close_cached_table(Table *table)
{

  wait_while_table_is_used(this, table, HA_EXTRA_FORCE_REOPEN);
  /* Close lock if this is not got with LOCK TABLES */
  if (lock)
  {
    mysql_unlock_tables(this, lock);
    lock= NULL;			// Start locked threads
  }
  /* Close all copies of 'table'.  This also frees all LOCK TABLES lock */
  unlink_open_table(table);

  /* When lock on LOCK_open is freed other threads can continue */
  broadcast_refresh();
}

/*
  RETURN VALUES
    false Message sent to net (admin operation went ok)
    true  Message should be sent by caller
          (admin operation or network communication failed)
*/
static bool mysql_admin_table(Session* session, TableList* tables,
                              HA_CHECK_OPT* check_opt,
                              const char *operator_name,
                              thr_lock_type lock_type,
                              bool open_for_modify,
                              bool no_warnings_for_error,
                              uint32_t extra_open_options,
                              int (*prepare_func)(Session *, TableList *,
                                                  HA_CHECK_OPT *),
                              int (handler::*operator_func)(Session *,
                                                            HA_CHECK_OPT *))
{
  TableList *table;
  Select_Lex *select= &session->lex->select_lex;
  List<Item> field_list;
  Item *item;
  plugin::Protocol *protocol= session->protocol;
  LEX *lex= session->lex;
  int result_code= 0;
  const CHARSET_INFO * const cs= system_charset_info;

  if (! session->endActiveTransaction())
    return 1;
  field_list.push_back(item = new Item_empty_string("Table",
                                                    NAME_CHAR_LEN * 2,
                                                    cs));
  item->maybe_null = 1;
  field_list.push_back(item = new Item_empty_string("Op", 10, cs));
  item->maybe_null = 1;
  field_list.push_back(item = new Item_empty_string("Msg_type", 10, cs));
  item->maybe_null = 1;
  field_list.push_back(item = new Item_empty_string("Msg_text", 255, cs));
  item->maybe_null = 1;
  if (protocol->sendFields(&field_list))
    return true;

  for (table= tables; table; table= table->next_local)
  {
    char table_name[NAME_LEN*2+2];
    char* db = table->db;
    bool fatal_error=0;

    sprintf(table_name,"%s.%s",db,table->table_name);
    session->open_options|= extra_open_options;
    table->lock_type= lock_type;
    /* open only one table from local list of command */
    {
      TableList *save_next_global, *save_next_local;
      save_next_global= table->next_global;
      table->next_global= 0;
      save_next_local= table->next_local;
      table->next_local= 0;
      select->table_list.first= (unsigned char*)table;
      /*
        Time zone tables and SP tables can be add to lex->query_tables list,
        so it have to be prepared.
        TODO: Investigate if we can put extra tables into argument instead of
        using lex->query_tables
      */
      lex->query_tables= table;
      lex->query_tables_last= &table->next_global;
      lex->query_tables_own_last= 0;
      session->no_warnings_for_error= no_warnings_for_error;

      session->openTablesLock(table);
      session->no_warnings_for_error= 0;
      table->next_global= save_next_global;
      table->next_local= save_next_local;
      session->open_options&= ~extra_open_options;
    }

    if (prepare_func)
    {
      switch ((*prepare_func)(session, table, check_opt)) {
      case  1:           // error, message written to net
        ha_autocommit_or_rollback(session, 1);
        session->endTransaction(ROLLBACK);
        session->close_thread_tables();
        continue;
      case -1:           // error, message could be written to net
        /* purecov: begin inspected */
        goto err;
        /* purecov: end */
      default:           // should be 0 otherwise
        ;
      }
    }

    /*
      CHECK Table command is only command where VIEW allowed here and this
      command use only temporary teble method for VIEWs resolving => there
      can't be VIEW tree substitition of join view => if opening table
      succeed then table->table will have real Table pointer as value (in
      case of join view substitution table->table can be 0, but here it is
      impossible)
    */
    if (!table->table)
    {
      if (!session->warn_list.elements)
        push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_ERROR,
                     ER_CHECK_NO_SUCH_TABLE, ER(ER_CHECK_NO_SUCH_TABLE));
      result_code= HA_ADMIN_CORRUPT;
      goto send_result;
    }

    if ((table->table->db_stat & HA_READ_ONLY) && open_for_modify)
    {
      /* purecov: begin inspected */
      char buff[FN_REFLEN + DRIZZLE_ERRMSG_SIZE];
      uint32_t length;
      protocol->prepareForResend();
      protocol->store(table_name);
      protocol->store(operator_name);
      protocol->store(STRING_WITH_LEN("error"));
      length= snprintf(buff, sizeof(buff), ER(ER_OPEN_AS_READONLY),
                       table_name);
      protocol->store(buff, length);
      ha_autocommit_or_rollback(session, 0);
      session->endTransaction(COMMIT);
      session->close_thread_tables();
      lex->reset_query_tables_list(false);
      table->table=0;				// For query cache
      if (protocol->write())
	goto err;
      continue;
      /* purecov: end */
    }

    /* Close all instances of the table to allow repair to rename files */
    if (lock_type == TL_WRITE && table->table->s->version)
    {
      pthread_mutex_lock(&LOCK_open); /* Lock type is TL_WRITE and we lock to repair the table */
      const char *old_message=session->enter_cond(&COND_refresh, &LOCK_open,
					      "Waiting to get writelock");
      mysql_lock_abort(session,table->table);
      remove_table_from_cache(session, table->table->s->db.str,
                              table->table->s->table_name.str,
                              RTFC_WAIT_OTHER_THREAD_FLAG |
                              RTFC_CHECK_KILLED_FLAG);
      session->exit_cond(old_message);
      if (session->killed)
	goto err;
      open_for_modify= 0;
    }

    if (table->table->s->crashed && operator_func == &handler::ha_check)
    {
      /* purecov: begin inspected */
      protocol->prepareForResend();
      protocol->store(table_name);
      protocol->store(operator_name);
      protocol->store(STRING_WITH_LEN("warning"));
      protocol->store(STRING_WITH_LEN("Table is marked as crashed"));
      if (protocol->write())
        goto err;
      /* purecov: end */
    }

    result_code = (table->table->file->*operator_func)(session, check_opt);

send_result:

    lex->cleanup_after_one_table_open();
    session->clear_error();  // these errors shouldn't get client
    {
      List_iterator_fast<DRIZZLE_ERROR> it(session->warn_list);
      DRIZZLE_ERROR *err;
      while ((err= it++))
      {
        protocol->prepareForResend();
        protocol->store(table_name);
        protocol->store(operator_name);
        protocol->store(warning_level_names[err->level].str,
                        warning_level_names[err->level].length);
        protocol->store(err->msg);
        if (protocol->write())
          goto err;
      }
      drizzle_reset_errors(session, true);
    }
    protocol->prepareForResend();
    protocol->store(table_name);
    protocol->store(operator_name);

send_result_message:

    switch (result_code) {
    case HA_ADMIN_NOT_IMPLEMENTED:
      {
	char buf[ERRMSGSIZE+20];
	uint32_t length=snprintf(buf, ERRMSGSIZE,
                             ER(ER_CHECK_NOT_IMPLEMENTED), operator_name);
	protocol->store(STRING_WITH_LEN("note"));
	protocol->store(buf, length);
      }
      break;

    case HA_ADMIN_OK:
      protocol->store(STRING_WITH_LEN("status"));
      protocol->store(STRING_WITH_LEN("OK"));
      break;

    case HA_ADMIN_FAILED:
      protocol->store(STRING_WITH_LEN("status"));
      protocol->store(STRING_WITH_LEN("Operation failed"));
      break;

    case HA_ADMIN_REJECT:
      protocol->store(STRING_WITH_LEN("status"));
      protocol->store(STRING_WITH_LEN("Operation need committed state"));
      open_for_modify= false;
      break;

    case HA_ADMIN_ALREADY_DONE:
      protocol->store(STRING_WITH_LEN("status"));
      protocol->store(STRING_WITH_LEN("Table is already up to date"));
      break;

    case HA_ADMIN_CORRUPT:
      protocol->store(STRING_WITH_LEN("error"));
      protocol->store(STRING_WITH_LEN("Corrupt"));
      fatal_error=1;
      break;

    case HA_ADMIN_INVALID:
      protocol->store(STRING_WITH_LEN("error"));
      protocol->store(STRING_WITH_LEN("Invalid argument"));
      break;

    case HA_ADMIN_TRY_ALTER:
    {
      /*
        This is currently used only by InnoDB. ha_innobase::optimize() answers
        "try with alter", so here we close the table, do an ALTER Table,
        reopen the table and do ha_innobase::analyze() on it.
      */
      ha_autocommit_or_rollback(session, 0);
      session->close_thread_tables();
      TableList *save_next_local= table->next_local,
                 *save_next_global= table->next_global;
      table->next_local= table->next_global= 0;
      result_code= mysql_recreate_table(session, table);
      /*
        mysql_recreate_table() can push OK or ERROR.
        Clear 'OK' status. If there is an error, keep it:
        we will store the error message in a result set row
        and then clear.
      */
      if (session->main_da.is_ok())
        session->main_da.reset_diagnostics_area();
      ha_autocommit_or_rollback(session, 0);
      session->close_thread_tables();
      if (!result_code) // recreation went ok
      {
        if ((table->table= session->openTableLock(table, lock_type)) &&
            ((result_code= table->table->file->ha_analyze(session, check_opt)) > 0))
          result_code= 0; // analyze went ok
      }
      if (result_code) // either mysql_recreate_table or analyze failed
      {
        assert(session->is_error());
        if (session->is_error())
        {
          const char *err_msg= session->main_da.message();
          if (!session->protocol->isConnected())
          {
            errmsg_printf(ERRMSG_LVL_ERROR, "%s", err_msg);
          }
          else
          {
            /* Hijack the row already in-progress. */
            protocol->store(STRING_WITH_LEN("error"));
            protocol->store(err_msg);
            (void)protocol->write();
            /* Start off another row for HA_ADMIN_FAILED */
            protocol->prepareForResend();
            protocol->store(table_name);
            protocol->store(operator_name);
          }
          session->clear_error();
        }
      }
      result_code= result_code ? HA_ADMIN_FAILED : HA_ADMIN_OK;
      table->next_local= save_next_local;
      table->next_global= save_next_global;
      goto send_result_message;
    }
    case HA_ADMIN_NEEDS_UPGRADE:
    case HA_ADMIN_NEEDS_ALTER:
    {
      char buf[ERRMSGSIZE];
      uint32_t length;

      protocol->store(STRING_WITH_LEN("error"));
      length=snprintf(buf, ERRMSGSIZE, ER(ER_TABLE_NEEDS_UPGRADE), table->table_name);
      protocol->store(buf, length);
      fatal_error=1;
      break;
    }

    default:				// Probably HA_ADMIN_INTERNAL_ERROR
      {
        char buf[ERRMSGSIZE+20];
        uint32_t length=snprintf(buf, ERRMSGSIZE,
                             _("Unknown - internal error %d during operation"),
                             result_code);
        protocol->store(STRING_WITH_LEN("error"));
        protocol->store(buf, length);
        fatal_error=1;
        break;
      }
    }
    if (table->table)
    {
      if (fatal_error)
        table->table->s->version=0;               // Force close of table
      else if (open_for_modify)
      {
        if (table->table->s->tmp_table)
          table->table->file->info(HA_STATUS_CONST);
        else
        {
          pthread_mutex_lock(&LOCK_open);
          remove_table_from_cache(session, table->table->s->db.str,
                                  table->table->s->table_name.str, RTFC_NO_FLAG);
          pthread_mutex_unlock(&LOCK_open);
        }
      }
    }
    ha_autocommit_or_rollback(session, 0);
    session->endTransaction(COMMIT);
    session->close_thread_tables();
    table->table=0;				// For query cache
    if (protocol->write())
      goto err;
  }

  session->my_eof();
  return(false);

err:
  ha_autocommit_or_rollback(session, 1);
  session->endTransaction(ROLLBACK);
  session->close_thread_tables();			// Shouldn't be needed
  if (table)
    table->table=0;
  return(true);
}

bool mysql_optimize_table(Session* session, TableList* tables, HA_CHECK_OPT* check_opt)
{
  return(mysql_admin_table(session, tables, check_opt,
                           "optimize", TL_WRITE, 1,0,0,0,
                           &handler::ha_optimize));
}

static bool mysql_create_like_schema_frm(Session* session,
                                         TableList* schema_table,
                                         HA_CREATE_INFO *create_info,
                                         drizzled::message::Table* table_proto)
{
  HA_CREATE_INFO local_create_info;
  Alter_info alter_info;
  bool tmp_table= (create_info->options & HA_LEX_CREATE_TMP_TABLE);
  uint32_t keys= schema_table->table->s->keys;
  uint32_t db_options= 0;

  memset(&local_create_info, 0, sizeof(local_create_info));
  local_create_info.db_type= schema_table->table->s->db_type();
  local_create_info.row_type= schema_table->table->s->row_type;
  local_create_info.default_table_charset=default_charset_info;
  alter_info.flags.set(ALTER_CHANGE_COLUMN);
  alter_info.flags.set(ALTER_RECREATE);
  schema_table->table->use_all_columns();
  if (mysql_prepare_alter_table(session, schema_table->table,
                                &local_create_info, &alter_info))
    return true;

  if (mysql_prepare_create_table(session, &local_create_info, &alter_info,
                                 tmp_table, &db_options,
                                 schema_table->table->file,
                                 &schema_table->table->s->key_info, &keys, 0))
    return true;

  local_create_info.max_rows= 0;

  table_proto->set_name("system_stupid_i_s_fix_nonsense");
  if(tmp_table)
    table_proto->set_type(drizzled::message::Table::TEMPORARY);
  else
    table_proto->set_type(drizzled::message::Table::STANDARD);

  {
    drizzled::message::Table::StorageEngine *protoengine;
    protoengine= table_proto->mutable_engine();

    StorageEngine *engine= local_create_info.db_type;

    protoengine->set_name(engine->getName());
  }

  if (fill_table_proto(table_proto, "system_stupid_i_s_fix_nonsense",
                       alter_info.create_list, &local_create_info,
                       keys, schema_table->table->s->key_info))
    return true;

  return false;
}

/*
  Create a table identical to the specified table

  SYNOPSIS
    mysql_create_like_table()
    session		Thread object
    table       Table list element for target table
    src_table   Table list element for source table
    create_info Create info

  RETURN VALUES
    false OK
    true  error
*/

bool mysql_create_like_table(Session* session, TableList* table, TableList* src_table,
                             HA_CREATE_INFO *create_info)
{
  Table *name_lock= 0;
  char src_path[FN_REFLEN], dst_path[FN_REFLEN];
  uint32_t dst_path_length;
  char *db= table->db;
  char *table_name= table->table_name;
  int  err;
  bool res= true;
  uint32_t not_used;
  drizzled::message::Table src_proto;

  /*
    By opening source table we guarantee that it exists and no concurrent
    DDL operation will mess with it. Later we also take an exclusive
    name-lock on target table name, which makes copying of .frm file,
    call to ha_create_table() and binlogging atomic against concurrent DML
    and DDL operations on target table. Thus by holding both these "locks"
    we ensure that our statement is properly isolated from all concurrent
    operations which matter.
  */
  if (session->open_tables_from_list(&src_table, &not_used))
    return true;

  strncpy(src_path, src_table->table->s->path.str, sizeof(src_path));

  /*
    Check that destination tables does not exist. Note that its name
    was already checked when it was added to the table list.
  */
  if (create_info->options & HA_LEX_CREATE_TMP_TABLE)
  {
    if (session->find_temporary_table(db, table_name))
      goto table_exists;
    dst_path_length= build_tmptable_filename(session, dst_path, sizeof(dst_path));
    create_info->table_options|= HA_CREATE_DELAY_KEY_WRITE;
  }
  else
  {
    if (session->lock_table_name_if_not_cached(db, table_name, &name_lock))
      goto err;
    if (!name_lock)
      goto table_exists;
    dst_path_length= build_table_filename(dst_path, sizeof(dst_path),
                                          db, table_name, false);
    if (StorageEngine::getTableProto(dst_path, NULL)==EEXIST)
      goto table_exists;
  }

  /*
    Create a new table by copying from source table

    Altough exclusive name-lock on target table protects us from concurrent
    DML and DDL operations on it we still want to wrap .FRM creation and call
    to ha_create_table() in critical section protected by LOCK_open in order
    to provide minimal atomicity against operations which disregard name-locks,
    like I_S implementation, for example. This is a temporary and should not
    be copied. Instead we should fix our code to always honor name-locks.

    Also some engines (e.g. NDB cluster) require that LOCK_open should be held
    during the call to ha_create_table(). See bug #28614 for more info.
  */
  pthread_mutex_lock(&LOCK_open); /* We lock for CREATE TABLE LIKE to copy table definition */

  {
    int protoerr= EEXIST;

    if (src_table->schema_table)
    {
      if (mysql_create_like_schema_frm(session, src_table, create_info,
                                     &src_proto))
      {
        pthread_mutex_unlock(&LOCK_open);
        goto err;
      }
    }
    else
    {
      protoerr= StorageEngine::getTableProto(src_path, &src_proto);
    }

    string dst_proto_path(dst_path);
    string file_ext = ".dfe";

    dst_proto_path.append(file_ext);

    if (protoerr == EEXIST)
    {
      StorageEngine* engine= ha_resolve_by_name(session,
                                                src_proto.engine().name());

      if (engine->check_flag(HTON_BIT_HAS_DATA_DICTIONARY) == false)
        protoerr= drizzle_write_proto_file(dst_proto_path.c_str(), &src_proto);
      else
        protoerr= 0;
    }

    if (protoerr)
    {
      if (my_errno == ENOENT)
        my_error(ER_BAD_DB_ERROR,MYF(0),db);
      else
        my_error(ER_CANT_CREATE_FILE,MYF(0),dst_path,my_errno);
      pthread_mutex_unlock(&LOCK_open);
      goto err;
    }
  }

  /*
    As mysql_truncate don't work on a new table at this stage of
    creation, instead create the table directly (for both normal
    and temporary tables).
  */

  err= ha_create_table(session, dst_path, db, table_name, create_info, 1,
                       &src_proto);
  pthread_mutex_unlock(&LOCK_open);

  if (create_info->options & HA_LEX_CREATE_TMP_TABLE)
  {
    if (err || !session->open_temporary_table(dst_path, db, table_name, 1, OTM_OPEN))
    {
      (void) session->rm_temporary_table(create_info->db_type, dst_path);
      goto err;     /* purecov: inspected */
    }
  }
  else if (err)
  {
    (void) quick_rm_table(create_info->db_type, db,
			  table_name, false); /* purecov: inspected */
    goto err;	    /* purecov: inspected */
  }

  /*
    We have to write the query before we unlock the tables.
  */
  {
    /*
       Since temporary tables are not replicated under row-based
       replication, CREATE TABLE ... LIKE ... needs special
       treatement.  We have four cases to consider, according to the
       following decision table:

           ==== ========= ========= ==============================
           Case    Target    Source Write to binary log
           ==== ========= ========= ==============================
           1       normal    normal Original statement
           2       normal temporary Generated statement
           3    temporary    normal Nothing
           4    temporary temporary Nothing
           ==== ========= ========= ==============================
    */
    if (!(create_info->options & HA_LEX_CREATE_TMP_TABLE))
    {
      if (src_table->table->s->tmp_table)               // Case 2
      {
        char buf[2048];
        String query(buf, sizeof(buf), system_charset_info);
        query.length(0);  // Have to zero it since constructor doesn't


        /*
          Here we open the destination table, on which we already have
          name-lock. This is needed for store_create_info() to work.
          The table will be closed by unlink_open_table() at the end
          of this function.
        */
        table->table= name_lock;
        pthread_mutex_lock(&LOCK_open); /* Open new table we have just acquired */
        if (session->reopen_name_locked_table(table, false))
        {
          pthread_mutex_unlock(&LOCK_open);
          goto err;
        }
        pthread_mutex_unlock(&LOCK_open);

        int result= store_create_info(table, &query, create_info);

        assert(result == 0); // store_create_info() always return 0
        write_bin_log(session, true, query.ptr(), query.length());
      }
      else                                      // Case 1
        write_bin_log(session, true, session->query, session->query_length);
    }
  }

  res= false;
  goto err;

table_exists:
  if (create_info->options & HA_LEX_CREATE_IF_NOT_EXISTS)
  {
    char warn_buff[DRIZZLE_ERRMSG_SIZE];
    snprintf(warn_buff, sizeof(warn_buff),
             ER(ER_TABLE_EXISTS_ERROR), table_name);
    push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
		 ER_TABLE_EXISTS_ERROR,warn_buff);
    res= false;
  }
  else
    my_error(ER_TABLE_EXISTS_ERROR, MYF(0), table_name);

err:
  if (name_lock)
  {
    pthread_mutex_lock(&LOCK_open); /* unlink open tables for create table like*/
    session->unlink_open_table(name_lock);
    pthread_mutex_unlock(&LOCK_open);
  }
  return(res);
}


bool mysql_analyze_table(Session* session, TableList* tables, HA_CHECK_OPT* check_opt)
{
  thr_lock_type lock_type = TL_READ_NO_INSERT;

  return(mysql_admin_table(session, tables, check_opt,
				"analyze", lock_type, 1, 0, 0, 0,
				&handler::ha_analyze));
}


bool mysql_check_table(Session* session, TableList* tables,HA_CHECK_OPT* check_opt)
{
  thr_lock_type lock_type = TL_READ_NO_INSERT;

  return(mysql_admin_table(session, tables, check_opt,
				"check", lock_type,
				0, 0, HA_OPEN_FOR_REPAIR, 0,
				&handler::ha_check));
}


/* table_list should contain just one table */
static int
mysql_discard_or_import_tablespace(Session *session,
                                   TableList *table_list,
                                   enum tablespace_op_type tablespace_op)
{
  Table *table;
  bool discard;
  int error;

  /*
    Note that DISCARD/IMPORT TABLESPACE always is the only operation in an
    ALTER Table
  */

  session->set_proc_info("discard_or_import_tablespace");

  discard= test(tablespace_op == DISCARD_TABLESPACE);

 /*
   We set this flag so that ha_innobase::open and ::external_lock() do
   not complain when we lock the table
 */
  session->tablespace_op= true;
  if (!(table= session->openTableLock(table_list, TL_WRITE)))
  {
    session->tablespace_op= false;
    return -1;
  }

  error= table->file->ha_discard_or_import_tablespace(discard);

  session->set_proc_info("end");

  if (error)
    goto err;

  /* The ALTER Table is always in its own transaction */
  error = ha_autocommit_or_rollback(session, 0);
  if (! session->endActiveTransaction())
    error=1;
  if (error)
    goto err;
  write_bin_log(session, false, session->query, session->query_length);

err:
  ha_autocommit_or_rollback(session, error);
  session->tablespace_op=false;

  if (error == 0)
  {
    session->my_ok();
    return 0;
  }

  table->file->print_error(error, MYF(0));

  return -1;
}


/*
  Manages enabling/disabling of indexes for ALTER Table

  SYNOPSIS
    alter_table_manage_keys()
      table                  Target table
      indexes_were_disabled  Whether the indexes of the from table
                             were disabled
      keys_onoff             ENABLE | DISABLE | LEAVE_AS_IS

  RETURN VALUES
    false  OK
    true   Error
*/

static
bool alter_table_manage_keys(Table *table, int indexes_were_disabled,
                             enum enum_enable_or_disable keys_onoff)
{
  int error= 0;
  switch (keys_onoff) {
  case ENABLE:
    error= table->file->ha_enable_indexes(HA_KEY_SWITCH_NONUNIQ_SAVE);
    break;
  case LEAVE_AS_IS:
    if (!indexes_were_disabled)
      break;
    /* fall-through: disabled indexes */
  case DISABLE:
    error= table->file->ha_disable_indexes(HA_KEY_SWITCH_NONUNIQ_SAVE);
  }

  if (error == HA_ERR_WRONG_COMMAND)
  {
    push_warning_printf(current_session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
                        ER_ILLEGAL_HA, ER(ER_ILLEGAL_HA),
                        table->s->table_name.str);
    error= 0;
  } else if (error)
    table->file->print_error(error, MYF(0));

  return(error);
}

static int 
create_temporary_table(Session *session,
                       Table *table,
                       char *new_db,
                       char *tmp_name,
                       HA_CREATE_INFO *create_info,
                       Alter_info *alter_info,
                       bool db_changed)
{
  int error;
  char index_file[FN_REFLEN], data_file[FN_REFLEN];
  StorageEngine *old_db_type, *new_db_type;
  old_db_type= table->s->db_type();
  new_db_type= create_info->db_type;
  /*
    Handling of symlinked tables:
    If no rename:
      Create new data file and index file on the same disk as the
      old data and index files.
      Copy data.
      Rename new data file over old data file and new index file over
      old index file.
      Symlinks are not changed.

   If rename:
      Create new data file and index file on the same disk as the
      old data and index files.  Create also symlinks to point at
      the new tables.
      Copy data.
      At end, rename intermediate tables, and symlinks to intermediate
      table, to final table name.
      Remove old table and old symlinks

    If rename is made to another database:
      Create new tables in new database.
      Copy data.
      Remove old table and symlinks.
  */
  if (db_changed)		// Ignore symlink if db changed
  {
    if (create_info->index_file_name)
    {
      /* Fix index_file_name to have 'tmp_name' as basename */
      strcpy(index_file, tmp_name);
      create_info->index_file_name=fn_same(index_file,
                                           create_info->index_file_name,
                                           1);
    }
    if (create_info->data_file_name)
    {
      /* Fix data_file_name to have 'tmp_name' as basename */
      strcpy(data_file, tmp_name);
      create_info->data_file_name=fn_same(data_file,
                                          create_info->data_file_name,
                                          1);
    }
  }
  else
    create_info->data_file_name=create_info->index_file_name=0;

  /*
    Create a table with a temporary name.
    We don't log the statement, it will be logged later.
  */
  drizzled::message::Table table_proto;
  table_proto.set_name(tmp_name);
  table_proto.set_type(drizzled::message::Table::TEMPORARY);

  drizzled::message::Table::StorageEngine *protoengine;
  protoengine= table_proto.mutable_engine();
  protoengine->set_name(new_db_type->getName());

  error= mysql_create_table(session, new_db, tmp_name,
                            create_info, &table_proto, alter_info, 1, 0);

  return(error);
}


/**
  Prepare column and key definitions for CREATE TABLE in ALTER Table.

  This function transforms parse output of ALTER Table - lists of
  columns and keys to add, drop or modify into, essentially,
  CREATE TABLE definition - a list of columns and keys of the new
  table. While doing so, it also performs some (bug not all)
  semantic checks.

  This function is invoked when we know that we're going to
  perform ALTER Table via a temporary table -- i.e. fast ALTER Table
  is not possible, perhaps because the ALTER statement contains
  instructions that require change in table data, not only in
  table definition or indexes.

  @param[in,out]  session         thread handle. Used as a memory pool
                              and source of environment information.
  @param[in]      table       the source table, open and locked
                              Used as an interface to the storage engine
                              to acquire additional information about
                              the original table.
  @param[in,out]  create_info A blob with CREATE/ALTER Table
                              parameters
  @param[in,out]  alter_info  Another blob with ALTER/CREATE parameters.
                              Originally create_info was used only in
                              CREATE TABLE and alter_info only in ALTER Table.
                              But since ALTER might end-up doing CREATE,
                              this distinction is gone and we just carry
                              around two structures.

  @return
    Fills various create_info members based on information retrieved
    from the storage engine.
    Sets create_info->varchar if the table has a VARCHAR column.
    Prepares alter_info->create_list and alter_info->key_list with
    columns and keys of the new table.
  @retval true   error, out of memory or a semantical error in ALTER
                 Table instructions
  @retval false  success
*/

static bool
mysql_prepare_alter_table(Session *session, Table *table,
                          HA_CREATE_INFO *create_info,
                          Alter_info *alter_info)
{
  /* New column definitions are added here */
  List<CreateField> new_create_list;
  /* New key definitions are added here */
  List<Key> new_key_list;
  List_iterator<Alter_drop> drop_it(alter_info->drop_list);
  List_iterator<CreateField> def_it(alter_info->create_list);
  List_iterator<Alter_column> alter_it(alter_info->alter_list);
  List_iterator<Key> key_it(alter_info->key_list);
  List_iterator<CreateField> find_it(new_create_list);
  List_iterator<CreateField> field_it(new_create_list);
  List<Key_part_spec> key_parts;
  uint32_t db_create_options= (table->s->db_create_options
                           & ~(HA_OPTION_PACK_RECORD));
  uint32_t used_fields= create_info->used_fields;
  KEY *key_info=table->key_info;
  bool rc= true;


  create_info->varchar= false;
  /* Let new create options override the old ones */
  if (!(used_fields & HA_CREATE_USED_MIN_ROWS))
    create_info->min_rows= table->s->min_rows;
  if (!(used_fields & HA_CREATE_USED_MAX_ROWS))
    create_info->max_rows= table->s->max_rows;
  if (!(used_fields & HA_CREATE_USED_AVG_ROW_LENGTH))
    create_info->avg_row_length= table->s->avg_row_length;
  if (!(used_fields & HA_CREATE_USED_BLOCK_SIZE))
    create_info->block_size= table->s->block_size;
  if (!(used_fields & HA_CREATE_USED_DEFAULT_CHARSET))
    create_info->default_table_charset= table->s->table_charset;
  if (!(used_fields & HA_CREATE_USED_AUTO) && table->found_next_number_field)
    {
    /* Table has an autoincrement, copy value to new table */
    table->file->info(HA_STATUS_AUTO);
    create_info->auto_increment_value= table->file->stats.auto_increment_value;
  }
  if (!(used_fields & HA_CREATE_USED_KEY_BLOCK_SIZE))
    create_info->key_block_size= table->s->key_block_size;

  table->restoreRecordAsDefault();     // Empty record for DEFAULT
  CreateField *def;

    /*
    First collect all fields from table which isn't in drop_list
    */
  Field **f_ptr,*field;
  for (f_ptr=table->field ; (field= *f_ptr) ; f_ptr++)
  {
    /* Check if field should be dropped */
    Alter_drop *drop;
    drop_it.rewind();
    while ((drop=drop_it++))
    {
      if (drop->type == Alter_drop::COLUMN &&
	  !my_strcasecmp(system_charset_info,field->field_name, drop->name))
      {
	/* Reset auto_increment value if it was dropped */
	if (MTYP_TYPENR(field->unireg_check) == Field::NEXT_NUMBER &&
	    !(used_fields & HA_CREATE_USED_AUTO))
	{
	  create_info->auto_increment_value=0;
	  create_info->used_fields|=HA_CREATE_USED_AUTO;
	}
	break;
      }
    }
    if (drop)
    {
      drop_it.remove();
      continue;
    }
    
    /* Mark that we will read the field */
    field->setReadSet();

    /* Check if field is changed */
    def_it.rewind();
    while ((def=def_it++))
    {
      if (def->change &&
	  !my_strcasecmp(system_charset_info,field->field_name, def->change))
	break;
    }
    if (def)
    {						// Field is changed
      def->field=field;
      if (!def->after)
      {
	new_create_list.push_back(def);
	def_it.remove();
      }
    }
    else
    {
      /*
        This field was not dropped and not changed, add it to the list
        for the new table.
      */
      def= new CreateField(field, field);
      new_create_list.push_back(def);
      alter_it.rewind();			// Change default if ALTER
      Alter_column *alter;
      while ((alter=alter_it++))
      {
	if (!my_strcasecmp(system_charset_info,field->field_name, alter->name))
	  break;
      }
      if (alter)
      {
	if (def->sql_type == DRIZZLE_TYPE_BLOB)
	{
	  my_error(ER_BLOB_CANT_HAVE_DEFAULT, MYF(0), def->change);
          goto err;
	}
	if ((def->def=alter->def))              // Use new default
          def->flags&= ~NO_DEFAULT_VALUE_FLAG;
        else
          def->flags|= NO_DEFAULT_VALUE_FLAG;
	alter_it.remove();
      }
    }
  }
  def_it.rewind();
  while ((def=def_it++))			// Add new columns
  {
    if (def->change && ! def->field)
    {
      my_error(ER_BAD_FIELD_ERROR, MYF(0), def->change, table->s->table_name.str);
      goto err;
    }
    /*
      Check that the DATE/DATETIME not null field we are going to add is
      either has a default value or the '0000-00-00' is allowed by the
      set sql mode.
      If the '0000-00-00' value isn't allowed then raise the error_if_not_empty
      flag to allow ALTER Table only if the table to be altered is empty.
    */
    if ((def->sql_type == DRIZZLE_TYPE_DATE ||
         def->sql_type == DRIZZLE_TYPE_DATETIME) &&
	!alter_info->datetime_field &&
	!(~def->flags & (NO_DEFAULT_VALUE_FLAG | NOT_NULL_FLAG)) &&
	session->variables.sql_mode & MODE_NO_ZERO_DATE)
    {
      alter_info->datetime_field= def;
      alter_info->error_if_not_empty= true;
    }
    if (!def->after)
      new_create_list.push_back(def);
    else if (def->after == first_keyword)
      new_create_list.push_front(def);
    else
    {
      CreateField *find;
      find_it.rewind();
      while ((find=find_it++))			// Add new columns
      {
	if (!my_strcasecmp(system_charset_info,def->after, find->field_name))
	  break;
      }
      if (!find)
      {
	my_error(ER_BAD_FIELD_ERROR, MYF(0), def->after, table->s->table_name.str);
	goto err;
      }
      find_it.after(def);			// Put element after this
      /*
        XXX: hack for Bug#28427.
        If column order has changed, force OFFLINE ALTER Table
        without querying engine capabilities.  If we ever have an
        engine that supports online ALTER Table CHANGE COLUMN
        <name> AFTER <name1> (Falcon?), this fix will effectively
        disable the capability.
        TODO: detect the situation in compare_tables, behave based
        on engine capabilities.
      */
      if (alter_info->build_method == HA_BUILD_ONLINE)
      {
        my_error(ER_NOT_SUPPORTED_YET, MYF(0), session->query);
        goto err;
      }
      alter_info->build_method= HA_BUILD_OFFLINE;
    }
  }
  if (alter_info->alter_list.elements)
  {
    my_error(ER_BAD_FIELD_ERROR, MYF(0),
             alter_info->alter_list.head()->name, table->s->table_name.str);
    goto err;
  }
  if (!new_create_list.elements)
  {
    my_message(ER_CANT_REMOVE_ALL_FIELDS, ER(ER_CANT_REMOVE_ALL_FIELDS),
               MYF(0));
    goto err;
  }

  /*
    Collect all keys which isn't in drop list. Add only those
    for which some fields exists.
  */

  for (uint32_t i=0 ; i < table->s->keys ; i++,key_info++)
  {
    char *key_name= key_info->name;
    Alter_drop *drop;
    drop_it.rewind();
    while ((drop=drop_it++))
    {
      if (drop->type == Alter_drop::KEY &&
	  !my_strcasecmp(system_charset_info,key_name, drop->name))
	break;
    }
    if (drop)
    {
      drop_it.remove();
      continue;
    }

    KEY_PART_INFO *key_part= key_info->key_part;
    key_parts.empty();
    for (uint32_t j=0 ; j < key_info->key_parts ; j++,key_part++)
    {
      if (!key_part->field)
	continue;				// Wrong field (from UNIREG)
      const char *key_part_name=key_part->field->field_name;
      CreateField *cfield;
      field_it.rewind();
      while ((cfield=field_it++))
      {
	if (cfield->change)
	{
	  if (!my_strcasecmp(system_charset_info, key_part_name,
			     cfield->change))
	    break;
	}
	else if (!my_strcasecmp(system_charset_info,
				key_part_name, cfield->field_name))
	  break;
      }
      if (!cfield)
	continue;				// Field is removed
      uint32_t key_part_length=key_part->length;
      if (cfield->field)			// Not new field
      {
        /*
          If the field can't have only a part used in a key according to its
          new type, or should not be used partially according to its
          previous type, or the field length is less than the key part
          length, unset the key part length.

          We also unset the key part length if it is the same as the
          old field's length, so the whole new field will be used.

          BLOBs may have cfield->length == 0, which is why we test it before
          checking whether cfield->length < key_part_length (in chars).
         */
        if (!Field::type_can_have_key_part(cfield->field->type()) ||
            !Field::type_can_have_key_part(cfield->sql_type) ||
            (cfield->field->field_length == key_part_length &&
             !f_is_blob(key_part->key_type)) ||
	    (cfield->length && (cfield->length < key_part_length /
                                key_part->field->charset()->mbmaxlen)))
	  key_part_length= 0;			// Use whole field
      }
      key_part_length /= key_part->field->charset()->mbmaxlen;
      key_parts.push_back(new Key_part_spec(cfield->field_name,
                                            strlen(cfield->field_name),
					    key_part_length));
    }
    if (key_parts.elements)
    {
      KEY_CREATE_INFO key_create_info;
      Key *key;
      enum Key::Keytype key_type;
      memset(&key_create_info, 0, sizeof(key_create_info));

      key_create_info.algorithm= key_info->algorithm;
      if (key_info->flags & HA_USES_BLOCK_SIZE)
        key_create_info.block_size= key_info->block_size;
      if (key_info->flags & HA_USES_COMMENT)
        key_create_info.comment= key_info->comment;

      if (key_info->flags & HA_NOSAME)
      {
        if (is_primary_key_name(key_name))
          key_type= Key::PRIMARY;
        else
          key_type= Key::UNIQUE;
      }
      else
        key_type= Key::MULTIPLE;

      key= new Key(key_type, key_name, strlen(key_name),
                   &key_create_info,
                   test(key_info->flags & HA_GENERATED_KEY),
                   key_parts);
      new_key_list.push_back(key);
    }
  }
  {
    Key *key;
    while ((key=key_it++))			// Add new keys
    {
      if (key->type == Key::FOREIGN_KEY &&
          ((Foreign_key *)key)->validate(new_create_list))
        goto err;
      if (key->type != Key::FOREIGN_KEY)
        new_key_list.push_back(key);
      if (key->name.str && is_primary_key_name(key->name.str))
      {
	my_error(ER_WRONG_NAME_FOR_INDEX, MYF(0), key->name.str);
        goto err;
      }
    }
  }

  if (alter_info->drop_list.elements)
  {
    my_error(ER_CANT_DROP_FIELD_OR_KEY, MYF(0),
             alter_info->drop_list.head()->name);
    goto err;
  }
  if (alter_info->alter_list.elements)
  {
    my_error(ER_CANT_DROP_FIELD_OR_KEY, MYF(0),
             alter_info->alter_list.head()->name);
    goto err;
  }

  if (!create_info->comment.str)
  {
    create_info->comment.str= table->s->comment.str;
    create_info->comment.length= table->s->comment.length;
  }

  table->file->update_create_info(create_info);
  if ((create_info->table_options &
       (HA_OPTION_PACK_KEYS | HA_OPTION_NO_PACK_KEYS)) ||
      (used_fields & HA_CREATE_USED_PACK_KEYS))
    db_create_options&= ~(HA_OPTION_PACK_KEYS | HA_OPTION_NO_PACK_KEYS);
  if (create_info->table_options &
      (HA_OPTION_CHECKSUM | HA_OPTION_NO_CHECKSUM))
    db_create_options&= ~(HA_OPTION_CHECKSUM | HA_OPTION_NO_CHECKSUM);
  if (create_info->table_options &
      (HA_OPTION_DELAY_KEY_WRITE | HA_OPTION_NO_DELAY_KEY_WRITE))
    db_create_options&= ~(HA_OPTION_DELAY_KEY_WRITE |
			  HA_OPTION_NO_DELAY_KEY_WRITE);
  create_info->table_options|= db_create_options;

  if (table->s->tmp_table)
    create_info->options|=HA_LEX_CREATE_TMP_TABLE;

  rc= false;
  alter_info->create_list.swap(new_create_list);
  alter_info->key_list.swap(new_key_list);
err:
  return(rc);
}


/*
  Alter table

  SYNOPSIS
    mysql_alter_table()
      session              Thread handle
      new_db           If there is a RENAME clause
      new_name         If there is a RENAME clause
      create_info      Information from the parsing phase about new
                       table properties.
      table_list       The table to change.
      alter_info       Lists of fields, keys to be changed, added
                       or dropped.
      order_num        How many order_st BY fields has been specified.
      order            List of fields to order_st BY.
      ignore           Whether we have ALTER IGNORE Table

  DESCRIPTION
    This is a veery long function and is everything but the kitchen sink :)
    It is used to alter a table and not only by ALTER Table but also
    CREATE|DROP INDEX are mapped on this function.

    When the ALTER Table statement just does a RENAME or ENABLE|DISABLE KEYS,
    or both, then this function short cuts its operation by renaming
    the table and/or enabling/disabling the keys. In this case, the FRM is
    not changed, directly by mysql_alter_table. However, if there is a
    RENAME + change of a field, or an index, the short cut is not used.
    See how `create_list` is used to generate the new FRM regarding the
    structure of the fields. The same is done for the indices of the table.

    Important is the fact, that this function tries to do as little work as
    possible, by finding out whether a intermediate table is needed to copy
    data into and when finishing the altering to use it as the original table.
    For this reason the function compare_tables() is called, which decides
    based on all kind of data how similar are the new and the original
    tables.

  RETURN VALUES
    false  OK
    true   Error
*/

bool mysql_alter_table(Session *session, 
                       char *new_db, 
                       char *new_name,
                       HA_CREATE_INFO *create_info,
                       TableList *table_list,
                       Alter_info *alter_info,
                       uint32_t order_num, 
                       order_st *order, 
                       bool ignore)
{
  Table *table;
  Table *new_table= NULL;
  Table *name_lock= NULL;
  string new_name_str;
  int error= 0;
  char tmp_name[80];
  char old_name[32];
  char new_name_buff[FN_REFLEN];
  char new_alias_buff[FN_REFLEN];
  char *table_name;
  char *db;
  const char *new_alias;
  char path[FN_REFLEN];
  ha_rows copied= 0;
  ha_rows deleted= 0;
  StorageEngine *old_db_type;
  StorageEngine *new_db_type;
  StorageEngine *save_old_db_type;
  bitset<32> tmp;

  new_name_buff[0]= '\0';

  if (table_list && table_list->schema_table)
  {
    my_error(ER_DBACCESS_DENIED_ERROR, MYF(0), "", "", INFORMATION_SCHEMA_NAME.c_str());
    return true;
  }

  session->set_proc_info("init");

  /*
    Assign variables table_name, new_name, db, new_db, path
    to simplify further comparisons: we want to see if it's a RENAME
    later just by comparing the pointers, avoiding the need for strcmp.
  */
  table_name= table_list->table_name;
  db= table_list->db;
  if (! new_db || ! my_strcasecmp(table_alias_charset, new_db, db))
    new_db= db;

  if (alter_info->tablespace_op != NO_TABLESPACE_OP)
  {
    /* DISCARD/IMPORT TABLESPACE is always alone in an ALTER Table */
    return mysql_discard_or_import_tablespace(session, table_list, alter_info->tablespace_op);
  }

  build_table_filename(path, sizeof(path), db, table_name, false);

  ostringstream oss;
  oss << drizzle_data_home << "/" << db << "/" << table_name;

  (void) unpack_filename(new_name_buff, oss.str().c_str());

  /*
    If this is just a rename of a view, short cut to the
    following scenario: 1) lock LOCK_open 2) do a RENAME
    2) unlock LOCK_open.
    This is a copy-paste added to make sure
    ALTER (sic:) Table .. RENAME works for views. ALTER VIEW is handled
    as an independent branch in mysql_execute_command. The need
    for a copy-paste arose because the main code flow of ALTER Table
    ... RENAME tries to use openTableLock, which does not work for views
    (openTableLock was never modified to merge table lists of child tables
    into the main table list, like open_tables does).
    This code is wrong and will be removed, please do not copy.
  */

  if (!(table= session->openTableLock(table_list, TL_WRITE_ALLOW_READ)))
    return true;
  
  table->use_all_columns();

  /* Check that we are not trying to rename to an existing table */
  if (new_name)
  {
    strcpy(new_name_buff, new_name);
    strcpy(new_alias_buff, new_name);
    new_alias= new_alias_buff;

    my_casedn_str(files_charset_info, new_name_buff);
    new_alias= new_name; // Create lower case table name
    my_casedn_str(files_charset_info, new_name);

    if (new_db == db &&
        ! my_strcasecmp(table_alias_charset, new_name_buff, table_name))
    {
      /*
        Source and destination table names are equal: make later check
        easier.
      */
      new_alias= new_name= table_name;
    }
    else
    {
      if (table->s->tmp_table != NO_TMP_TABLE)
      {
        if (session->find_temporary_table(new_db, new_name_buff))
        {
          my_error(ER_TABLE_EXISTS_ERROR, MYF(0), new_name_buff);
          return true;
        }
      }
      else
      {
        if (session->lock_table_name_if_not_cached(new_db, new_name, &name_lock))
          return true;

        if (! name_lock)
        {
          my_error(ER_TABLE_EXISTS_ERROR, MYF(0), new_alias);
          return true;
        }

        build_table_filename(new_name_buff, sizeof(new_name_buff), new_db, new_name_buff, false);

        if (StorageEngine::getTableProto(new_name_buff, NULL) == EEXIST)
        {
          /* Table will be closed by Session::executeCommand() */
          my_error(ER_TABLE_EXISTS_ERROR, MYF(0), new_alias);
          goto err;
        }
      }
    }
  }
  else
  {
    new_alias= table_name;
    new_name= table_name;
  }

  old_db_type= table->s->db_type();
  if (! create_info->db_type)
  {
    create_info->db_type= old_db_type;
  }

  if (table->s->tmp_table != NO_TMP_TABLE)
    create_info->options|= HA_LEX_CREATE_TMP_TABLE;

  if (check_engine(session, new_name, create_info))
    goto err;

  new_db_type= create_info->db_type;

  if (new_db_type != old_db_type &&
      !table->file->can_switch_engines())
  {
    assert(0);
    my_error(ER_ROW_IS_REFERENCED, MYF(0));
    goto err;
  }

  if (create_info->row_type == ROW_TYPE_NOT_USED)
    create_info->row_type= table->s->row_type;

  if (old_db_type->check_flag(HTON_BIT_ALTER_NOT_SUPPORTED) ||
      new_db_type->check_flag(HTON_BIT_ALTER_NOT_SUPPORTED))
  {
    my_error(ER_ILLEGAL_HA, MYF(0), table_name);
    goto err;
  }

  session->set_proc_info("setup");
  
  /*
   * test if no other bits except ALTER_RENAME and ALTER_KEYS_ONOFF are set
   */
  tmp.set();
  tmp.reset(ALTER_RENAME);
  tmp.reset(ALTER_KEYS_ONOFF);
  tmp&= alter_info->flags;
  if (! (tmp.any()) &&
      ! table->s->tmp_table) // no need to touch frm
  {
    switch (alter_info->keys_onoff)
    {
    case LEAVE_AS_IS:
      break;
    case ENABLE:
      /*
        wait_while_table_is_used() ensures that table being altered is
        opened only by this thread and that Table::TableShare::version
        of Table object corresponding to this table is 0.
        The latter guarantees that no DML statement will open this table
        until ALTER Table finishes (i.e. until close_thread_tables())
        while the fact that the table is still open gives us protection
        from concurrent DDL statements.
      */
      pthread_mutex_lock(&LOCK_open); /* DDL wait for/blocker */
      wait_while_table_is_used(session, table, HA_EXTRA_FORCE_REOPEN);
      pthread_mutex_unlock(&LOCK_open);
      error= table->file->ha_enable_indexes(HA_KEY_SWITCH_NONUNIQ_SAVE);
      /* COND_refresh will be signaled in close_thread_tables() */
      break;
    case DISABLE:
      pthread_mutex_lock(&LOCK_open); /* DDL wait for/blocker */
      wait_while_table_is_used(session, table, HA_EXTRA_FORCE_REOPEN);
      pthread_mutex_unlock(&LOCK_open);
      error=table->file->ha_disable_indexes(HA_KEY_SWITCH_NONUNIQ_SAVE);
      /* COND_refresh will be signaled in close_thread_tables() */
      break;
    default:
      assert(false);
      error= 0;
      break;
    }

    if (error == HA_ERR_WRONG_COMMAND)
    {
      error= 0;
      push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
			  ER_ILLEGAL_HA, ER(ER_ILLEGAL_HA),
			  table->alias);
    }

    pthread_mutex_lock(&LOCK_open); /* Lock to remove all instances of table from table cache before ALTER */
    /*
      Unlike to the above case close_cached_table() below will remove ALL
      instances of Table from table cache (it will also remove table lock
      held by this thread). So to make actual table renaming and writing
      to binlog atomic we have to put them into the same critical section
      protected by LOCK_open mutex. This also removes gap for races between
      access() and mysql_rename_table() calls.
    */

    if (error == 0 && 
        (new_name != table_name || new_db != db))
    {
      session->set_proc_info("rename");
      /*
        Then do a 'simple' rename of the table. First we need to close all
        instances of 'source' table.
      */
      session->close_cached_table(table);
      /*
        Then, we want check once again that target table does not exist.
        Actually the order of these two steps does not matter since
        earlier we took name-lock on the target table, so we do them
        in this particular order only to be consistent with 5.0, in which
        we don't take this name-lock and where this order really matters.
        TODO: Investigate if we need this access() check at all.
      */
      if (StorageEngine::getTableProto(new_name, NULL) == EEXIST)
      {
        my_error(ER_TABLE_EXISTS_ERROR, MYF(0), new_name);
        error= -1;
      }
      else
      {
        *fn_ext(new_name)= 0;
        if (mysql_rename_table(old_db_type, db, table_name, new_db, new_alias, 0))
          error= -1;
      }
    }

    if (error == HA_ERR_WRONG_COMMAND)
    {
      error= 0;
      push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
                          ER_ILLEGAL_HA, ER(ER_ILLEGAL_HA),
                          table->alias);
    }

    if (error == 0)
    {
      write_bin_log(session, true, session->query, session->query_length);
      session->my_ok();
    }
    else if (error > 0)
    {
      table->file->print_error(error, MYF(0));
      error= -1;
    }

    if (name_lock)
      session->unlink_open_table(name_lock);

    pthread_mutex_unlock(&LOCK_open);
    table_list->table= NULL;
    return error;
  }

  /* We have to do full alter table. */

  /*
    If the old table had partitions and we are doing ALTER Table ...
    engine= <new_engine>, the new table must preserve the original
    partitioning. That means that the new engine is still the
    partitioning engine, not the engine specified in the parser.
    This is discovered  in prep_alter_part_table, which in such case
    updates create_info->db_type.
    Now we need to update the stack copy of create_info->db_type,
    as otherwise we won't be able to correctly move the files of the
    temporary table to the result table files.
  */
  new_db_type= create_info->db_type;

  if (mysql_prepare_alter_table(session, table, create_info, alter_info))
    goto err;

  set_table_default_charset(create_info, db);

  alter_info->build_method= HA_BUILD_OFFLINE;

  snprintf(tmp_name, sizeof(tmp_name), "%s-%lx_%"PRIx64, TMP_FILE_PREFIX, (unsigned long) current_pid, session->thread_id);
  
  /* Safety fix for innodb */
  my_casedn_str(files_charset_info, tmp_name);

  /* Create a temporary table with the new format */
  error= create_temporary_table(session, table, new_db, tmp_name, create_info, alter_info, ! strcmp(db, new_db));

  if (error != 0)
    goto err;

  /* Open the table so we need to copy the data to it. */
  if (table->s->tmp_table)
  {
    TableList tbl;
    tbl.db= new_db;
    tbl.alias= tmp_name;
    tbl.table_name= tmp_name;

    /* Table is in session->temporary_tables */
    new_table= session->openTable(&tbl, (bool*) 0, DRIZZLE_LOCK_IGNORE_FLUSH);
  }
  else
  {
    char tmp_path[FN_REFLEN];
    /* table is a normal table: Create temporary table in same directory */
    build_table_filename(tmp_path, sizeof(tmp_path), new_db, tmp_name, true);
    /* Open our intermediate table */
    new_table= session->open_temporary_table(tmp_path, new_db, tmp_name, 0, OTM_OPEN);
  }

  if (new_table == NULL)
    goto err1;

  /* Copy the data if necessary. */
  session->count_cuted_fields= CHECK_FIELD_WARN;	// calc cuted fields
  session->cuted_fields= 0L;
  session->set_proc_info("copy to tmp table");
  copied= deleted= 0;

  assert(new_table);

  /* We don't want update TIMESTAMP fields during ALTER Table. */
  new_table->timestamp_field_type= TIMESTAMP_NO_AUTO_SET;
  new_table->next_number_field= new_table->found_next_number_field;
  error= copy_data_between_tables(table, 
                                  new_table,
                                  alter_info->create_list, 
                                  ignore,
                                  order_num, 
                                  order, 
                                  &copied, 
                                  &deleted,
                                  alter_info->keys_onoff,
                                  alter_info->error_if_not_empty);

  /* We must not ignore bad input! */
  session->count_cuted_fields= CHECK_FIELD_ERROR_FOR_NULL;

  if (table->s->tmp_table != NO_TMP_TABLE)
  {
    /* We changed a temporary table */
    if (error)
      goto err1;

    /* Close lock if this is a transactional table */
    if (session->lock)
    {
      mysql_unlock_tables(session, session->lock);
      session->lock= 0;
    }

    /* Remove link to old table and rename the new one */
    session->close_temporary_table(table, true, true);

    /* Should pass the 'new_name' as we store table name in the cache */
    if (new_table->rename_temporary_table(new_db, new_name))
      goto err1;
    
    goto end_temporary;
  }

  if (new_table)
  {
    /*
      Close the intermediate table that will be the new table.
      Note that MERGE tables do not have their children attached here.
    */
    new_table->intern_close_table();
    free(new_table);
  }

  pthread_mutex_lock(&LOCK_open); /* ALTER TABLE */
  
  if (error)
  {
    quick_rm_table(new_db_type, new_db, tmp_name, true);
    pthread_mutex_unlock(&LOCK_open);
    goto err;
  }

  /*
    Data is copied. Now we:
    1) Wait until all other threads close old version of table.
    2) Close instances of table open by this thread and replace them
       with exclusive name-locks.
    3) Rename the old table to a temp name, rename the new one to the
       old name.
    4) If we are under LOCK TABLES and don't do ALTER Table ... RENAME
       we reopen new version of table.
    5) Write statement to the binary log.
    6) If we are under LOCK TABLES and do ALTER Table ... RENAME we
       remove name-locks from list of open tables and table cache.
    7) If we are not not under LOCK TABLES we rely on close_thread_tables()
       call to remove name-locks from table cache and list of open table.
  */

  session->set_proc_info("rename result table");

  snprintf(old_name, sizeof(old_name), "%s2-%lx-%"PRIx64, TMP_FILE_PREFIX, (unsigned long) current_pid, session->thread_id);

  my_casedn_str(files_charset_info, old_name);

  wait_while_table_is_used(session, table, HA_EXTRA_PREPARE_FOR_RENAME);
  session->close_data_files_and_morph_locks(db, table_name);

  error= 0;
  save_old_db_type= old_db_type;

  /*
    This leads to the storage engine (SE) not being notified for renames in
    mysql_rename_table(), because we just juggle with the FRM and nothing
    more. If we have an intermediate table, then we notify the SE that
    it should become the actual table. Later, we will recycle the old table.
    However, in case of ALTER Table RENAME there might be no intermediate
    table. This is when the old and new tables are compatible, according to
    compare_table(). Then, we need one additional call to
    mysql_rename_table() with flag NO_FRM_RENAME, which does nothing else but
    actual rename in the SE and the FRM is not touched. Note that, if the
    table is renamed and the SE is also changed, then an intermediate table
    is created and the additional call will not take place.
  */
  if (mysql_rename_table(old_db_type, db, table_name, db, old_name, FN_TO_IS_TMP))
  {
    error= 1;
    quick_rm_table(new_db_type, new_db, tmp_name, true);
  }
  else
  {
    if (mysql_rename_table(new_db_type, new_db, tmp_name, new_db, new_alias, FN_FROM_IS_TMP) != 0)
    {
      /* Try to get everything back. */
      error= 1;
      quick_rm_table(new_db_type, new_db, new_alias, false);
      quick_rm_table(new_db_type, new_db, tmp_name, true);
      mysql_rename_table(old_db_type, db, old_name, db, table_name, FN_FROM_IS_TMP);
    }
  }

  if (error)
  {
    /* This shouldn't happen. But let us play it safe. */
    goto err_with_placeholders;
  }

  quick_rm_table(old_db_type, db, old_name, true);

  pthread_mutex_unlock(&LOCK_open);

  session->set_proc_info("end");

  write_bin_log(session, true, session->query, session->query_length);

  if (old_db_type->check_flag(HTON_BIT_FLUSH_AFTER_RENAME))
  {
    /*
      For the alter table to be properly flushed to the logs, we
      have to open the new table.  If not, we get a problem on server
      shutdown. But we do not need to attach MERGE children.
    */
    char table_path[FN_REFLEN];
    Table *t_table;
    build_table_filename(table_path, sizeof(table_path), new_db, table_name, false);
    t_table= session->open_temporary_table(table_path, new_db, tmp_name, false, OTM_OPEN);
    if (t_table)
    {
      t_table->intern_close_table();
      free(t_table);
    }
    else
      errmsg_printf(ERRMSG_LVL_WARN, _("Could not open table %s.%s after rename\n"), new_db, table_name);

    ha_flush_logs(old_db_type);
  }
  table_list->table= NULL;

end_temporary:
  /*
   * Field::store() may have called my_error().  If this is 
   * the case, we must not send an ok packet, since 
   * Diagnostics_area::is_set() will fail an assert.
   */
  if (! session->is_error())
  {
    snprintf(tmp_name, sizeof(tmp_name), ER(ER_INSERT_INFO),
            (ulong) (copied + deleted), (ulong) deleted,
            (ulong) session->cuted_fields);
    session->my_ok(copied + deleted, 0, 0L, tmp_name);
    session->some_tables_deleted=0;
    return false;
  }
  else
  {
    /* my_error() was called.  Return true (which means error...) */
    return true;
  }

err1:
  if (new_table)
  {
    /* close_temporary_table() frees the new_table pointer. */
    session->close_temporary_table(new_table, true, true);
  }
  else
    quick_rm_table(new_db_type, new_db, tmp_name, true);

err:
  /*
    No default value was provided for a DATE/DATETIME field, the
    current sql_mode doesn't allow the '0000-00-00' value and
    the table to be altered isn't empty.
    Report error here.
  */
  if (alter_info->error_if_not_empty && session->row_count)
  {
    const char *f_val= 0;
    enum enum_drizzle_timestamp_type t_type= DRIZZLE_TIMESTAMP_DATE;
    switch (alter_info->datetime_field->sql_type)
    {
      case DRIZZLE_TYPE_DATE:
        f_val= "0000-00-00";
        t_type= DRIZZLE_TIMESTAMP_DATE;
        break;
      case DRIZZLE_TYPE_DATETIME:
        f_val= "0000-00-00 00:00:00";
        t_type= DRIZZLE_TIMESTAMP_DATETIME;
        break;
      default:
        /* Shouldn't get here. */
        assert(0);
    }
    bool save_abort_on_warning= session->abort_on_warning;
    session->abort_on_warning= true;
    make_truncated_value_warning(session, DRIZZLE_ERROR::WARN_LEVEL_ERROR,
                                 f_val, strlength(f_val), t_type,
                                 alter_info->datetime_field->field_name);
    session->abort_on_warning= save_abort_on_warning;
  }
  if (name_lock)
  {
    pthread_mutex_lock(&LOCK_open); /* ALTER TABLe */
    session->unlink_open_table(name_lock);
    pthread_mutex_unlock(&LOCK_open);
  }
  return true;

err_with_placeholders:
  /*
    An error happened while we were holding exclusive name-lock on table
    being altered. To be safe under LOCK TABLES we should remove placeholders
    from list of open tables list and table cache.
  */
  session->unlink_open_table(table);
  if (name_lock)
    session->unlink_open_table(name_lock);
  pthread_mutex_unlock(&LOCK_open);
  return true;
}
/* mysql_alter_table */

static int
copy_data_between_tables(Table *from,Table *to,
			 List<CreateField> &create,
                         bool ignore,
			 uint32_t order_num, order_st *order,
			 ha_rows *copied,
			 ha_rows *deleted,
                         enum enum_enable_or_disable keys_onoff,
                         bool error_if_not_empty)
{
  int error;
  CopyField *copy,*copy_end;
  ulong found_count,delete_count;
  Session *session= current_session;
  uint32_t length= 0;
  SORT_FIELD *sortorder;
  READ_RECORD info;
  TableList   tables;
  List<Item>   fields;
  List<Item>   all_fields;
  ha_rows examined_rows;
  bool auto_increment_field_copied= 0;
  uint64_t prev_insert_id;

  /*
    Turn off recovery logging since rollback of an alter table is to
    delete the new table so there is no need to log the changes to it.

    This needs to be done before external_lock
  */
  error= ha_enable_transaction(session, false);
  if (error)
    return -1;

  if (!(copy= new CopyField[to->s->fields]))
    return -1;				/* purecov: inspected */

  if (to->file->ha_external_lock(session, F_WRLCK))
    return -1;

  /* We need external lock before we can disable/enable keys */
  alter_table_manage_keys(to, from->file->indexes_are_disabled(), keys_onoff);

  /* We can abort alter table for any table type */
  session->abort_on_warning= !ignore;

  from->file->info(HA_STATUS_VARIABLE | HA_STATUS_NO_LOCK);
  to->file->ha_start_bulk_insert(from->file->stats.records);

  List_iterator<CreateField> it(create);
  CreateField *def;
  copy_end=copy;
  for (Field **ptr=to->field ; *ptr ; ptr++)
  {
    def=it++;
    if (def->field)
    {
      if (*ptr == to->next_number_field)
        auto_increment_field_copied= true;

      (copy_end++)->set(*ptr,def->field,0);
    }

  }

  found_count=delete_count=0;

  if (order)
  {
    if (to->s->primary_key != MAX_KEY && to->file->primary_key_is_clustered())
    {
      char warn_buff[DRIZZLE_ERRMSG_SIZE];
      snprintf(warn_buff, sizeof(warn_buff),
               _("order_st BY ignored because there is a user-defined clustered "
                 "index in the table '%-.192s'"),
               from->s->table_name.str);
      push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_WARN, ER_UNKNOWN_ERROR,
                   warn_buff);
    }
    else
    {
      from->sort.io_cache= new IO_CACHE;
      memset(from->sort.io_cache, 0, sizeof(IO_CACHE));

      memset(&tables, 0, sizeof(tables));
      tables.table= from;
      tables.alias= tables.table_name= from->s->table_name.str;
      tables.db= from->s->db.str;
      error= 1;

      if (session->lex->select_lex.setup_ref_array(session, order_num) ||
          setup_order(session, session->lex->select_lex.ref_pointer_array,
                      &tables, fields, all_fields, order) ||
          !(sortorder= make_unireg_sortorder(order, &length, NULL)) ||
          (from->sort.found_records= filesort(session, from, sortorder, length,
                                              (SQL_SELECT *) 0, HA_POS_ERROR,
                                              1, &examined_rows)) ==
          HA_POS_ERROR)
        goto err;
    }
  };

  /* Tell handler that we have values for all columns in the to table */
  to->use_all_columns();
  init_read_record(&info, session, from, (SQL_SELECT *) 0, 1,1);
  if (ignore)
    to->file->extra(HA_EXTRA_IGNORE_DUP_KEY);
  session->row_count= 0;
  to->restoreRecordAsDefault();        // Create empty record
  while (!(error=info.read_record(&info)))
  {
    if (session->killed)
    {
      session->send_kill_message();
      error= 1;
      break;
    }
    session->row_count++;
    /* Return error if source table isn't empty. */
    if (error_if_not_empty)
    {
      error= 1;
      break;
    }
    if (to->next_number_field)
    {
      if (auto_increment_field_copied)
        to->auto_increment_field_not_null= true;
      else
        to->next_number_field->reset();
    }

    for (CopyField *copy_ptr=copy ; copy_ptr != copy_end ; copy_ptr++)
    {
      copy_ptr->do_copy(copy_ptr);
    }
    prev_insert_id= to->file->next_insert_id;
    error=to->file->ha_write_row(to->record[0]);
    to->auto_increment_field_not_null= false;
    if (error)
    {
      if (!ignore ||
          to->file->is_fatal_error(error, HA_CHECK_DUP))
      {
         if (!to->file->is_fatal_error(error, HA_CHECK_DUP))
         {
           uint32_t key_nr= to->file->get_dup_key(error);
           if ((int) key_nr >= 0)
           {
             const char *err_msg= ER(ER_DUP_ENTRY_WITH_KEY_NAME);
             if (key_nr == 0 &&
                 (to->key_info[0].key_part[0].field->flags &
                  AUTO_INCREMENT_FLAG))
               err_msg= ER(ER_DUP_ENTRY_AUTOINCREMENT_CASE);
             to->file->print_keydup_error(key_nr, err_msg);
             break;
           }
         }

	to->file->print_error(error,MYF(0));
	break;
      }
      to->file->restore_auto_increment(prev_insert_id);
      delete_count++;
    }
    else
      found_count++;
  }
  end_read_record(&info);
  from->free_io_cache();
  delete [] copy;				// This is never 0

  if (to->file->ha_end_bulk_insert() && error <= 0)
  {
    to->file->print_error(my_errno,MYF(0));
    error=1;
  }
  to->file->extra(HA_EXTRA_NO_IGNORE_DUP_KEY);

  if (ha_enable_transaction(session, true))
  {
    error= 1;
    goto err;
  }

  /*
    Ensure that the new table is saved properly to disk so that we
    can do a rename
  */
  if (ha_autocommit_or_rollback(session, 0))
    error=1;
  if (! session->endActiveTransaction())
    error=1;

 err:
  session->abort_on_warning= 0;
  from->free_io_cache();
  *copied= found_count;
  *deleted=delete_count;
  to->file->ha_release_auto_increment();
  if (to->file->ha_external_lock(session,F_UNLCK))
    error=1;
  return(error > 0 ? -1 : 0);
}


/*
  Recreates tables by calling mysql_alter_table().

  SYNOPSIS
    mysql_recreate_table()
    session			Thread handler
    tables		Tables to recreate

 RETURN
    Like mysql_alter_table().
*/
bool mysql_recreate_table(Session *session, TableList *table_list)
{
  HA_CREATE_INFO create_info;
  Alter_info alter_info;

  assert(!table_list->next_global);
  /*
    table_list->table has been closed and freed. Do not reference
    uninitialized data. open_tables() could fail.
  */
  table_list->table= NULL;

  memset(&create_info, 0, sizeof(create_info));
  create_info.row_type=ROW_TYPE_NOT_USED;
  create_info.default_table_charset=default_charset_info;
  /* Force alter table to recreate table */
  alter_info.flags.set(ALTER_CHANGE_COLUMN);
  alter_info.flags.set(ALTER_RECREATE);
  return(mysql_alter_table(session, NULL, NULL, &create_info,
                                table_list, &alter_info, 0,
                                (order_st *) 0, 0));
}


bool mysql_checksum_table(Session *session, TableList *tables,
                          HA_CHECK_OPT *check_opt)
{
  TableList *table;
  List<Item> field_list;
  Item *item;
  plugin::Protocol *protocol= session->protocol;

  field_list.push_back(item = new Item_empty_string("Table", NAME_LEN*2));
  item->maybe_null= 1;
  field_list.push_back(item= new Item_int("Checksum", (int64_t) 1,
                                          MY_INT64_NUM_DECIMAL_DIGITS));
  item->maybe_null= 1;
  if (protocol->sendFields(&field_list))
    return true;

  /* Open one table after the other to keep lock time as short as possible. */
  for (table= tables; table; table= table->next_local)
  {
    char table_name[NAME_LEN*2+2];
    Table *t;

    sprintf(table_name,"%s.%s",table->db,table->table_name);

    t= table->table= session->openTableLock(table, TL_READ);
    session->clear_error();			// these errors shouldn't get client

    protocol->prepareForResend();
    protocol->store(table_name);

    if (!t)
    {
      /* Table didn't exist */
      protocol->store();
      session->clear_error();
    }
    else
    {
      if (t->file->ha_table_flags() & HA_HAS_CHECKSUM &&
	  !(check_opt->flags & T_EXTEND))
	protocol->store((uint64_t)t->file->checksum());
      else if (!(t->file->ha_table_flags() & HA_HAS_CHECKSUM) &&
	       (check_opt->flags & T_QUICK))
	protocol->store();
      else
      {
	/* calculating table's checksum */
	ha_checksum crc= 0;
        unsigned char null_mask=256 -  (1 << t->s->last_null_bit_pos);

        t->use_all_columns();

	if (t->file->ha_rnd_init(1))
	  protocol->store();
	else
	{
	  for (;;)
	  {
	    ha_checksum row_crc= 0;
            int error= t->file->rnd_next(t->record[0]);
            if (unlikely(error))
            {
              if (error == HA_ERR_RECORD_DELETED)
                continue;
              break;
            }
	    if (t->s->null_bytes)
            {
              /* fix undefined null bits */
              t->record[0][t->s->null_bytes-1] |= null_mask;
              if (!(t->s->db_create_options & HA_OPTION_PACK_RECORD))
                t->record[0][0] |= 1;

	      row_crc= my_checksum(row_crc, t->record[0], t->s->null_bytes);
            }

	    for (uint32_t i= 0; i < t->s->fields; i++ )
	    {
	      Field *f= t->field[i];
	      if ((f->type() == DRIZZLE_TYPE_BLOB) ||
                  (f->type() == DRIZZLE_TYPE_VARCHAR))
	      {
		String tmp;
		f->val_str(&tmp);
		row_crc= my_checksum(row_crc, (unsigned char*) tmp.ptr(), tmp.length());
	      }
	      else
		row_crc= my_checksum(row_crc, f->ptr,
				     f->pack_length());
	    }

	    crc+= row_crc;
	  }
	  protocol->store((uint64_t)crc);
          t->file->ha_rnd_end();
	}
      }
      session->clear_error();
      session->close_thread_tables();
      table->table=0;				// For query cache
    }
    if (protocol->write())
      goto err;
  }

  session->my_eof();
  return(false);

 err:
  session->close_thread_tables();			// Shouldn't be needed
  if (table)
    table->table=0;
  return(true);
}

static bool check_engine(Session *session, const char *table_name,
                         HA_CREATE_INFO *create_info)
{
  StorageEngine **new_engine= &create_info->db_type;
  StorageEngine *req_engine= *new_engine;
  if (!req_engine->is_enabled())
  {
    string engine_name= req_engine->getName();
    my_error(ER_FEATURE_DISABLED,MYF(0),
             engine_name.c_str(), engine_name.c_str());
             
    return true;
  }

  if (req_engine && req_engine != *new_engine)
  {
    push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
                       ER_WARN_USING_OTHER_HANDLER,
                       ER(ER_WARN_USING_OTHER_HANDLER),
                       ha_resolve_storage_engine_name(*new_engine).c_str(),
                       table_name);
  }
  if (create_info->options & HA_LEX_CREATE_TMP_TABLE &&
      (*new_engine)->check_flag(HTON_BIT_TEMPORARY_NOT_SUPPORTED))
  {
    if (create_info->used_fields & HA_CREATE_USED_ENGINE)
    {
      my_error(ER_ILLEGAL_HA_CREATE_OPTION, MYF(0),
               ha_resolve_storage_engine_name(*new_engine).c_str(),
               "TEMPORARY");
      *new_engine= 0;
      return true;
    }
    *new_engine= myisam_engine;
  }
  if(!(create_info->options & HA_LEX_CREATE_TMP_TABLE)
     && (*new_engine)->check_flag(HTON_BIT_TEMPORARY_ONLY))
  {
    my_error(ER_ILLEGAL_HA_CREATE_OPTION, MYF(0),
             ha_resolve_storage_engine_name(*new_engine).c_str(),
             "non-TEMPORARY");
    *new_engine= 0;
    return true;
  }

  return false;
}
