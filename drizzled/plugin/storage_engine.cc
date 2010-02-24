/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
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

#include "config.h"

#include <fcntl.h>
#include <unistd.h>

#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <functional>

#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "drizzled/my_hash.h"
#include "drizzled/cached_directory.h"

#include <drizzled/definitions.h>
#include <drizzled/base.h>
#include <drizzled/cursor.h>
#include <drizzled/plugin/storage_engine.h>
#include <drizzled/session.h>
#include <drizzled/error.h>
#include <drizzled/gettext.h>
#include <drizzled/unireg.h>
#include <drizzled/data_home.h>
#include "drizzled/errmsg_print.h"
#include "drizzled/xid.h"
#include "drizzled/sql_table.h"
#include "drizzled/global_charset_info.h"
#include "drizzled/charset.h"
#include "drizzled/internal/my_sys.h"
#include "drizzled/db.h"

#include <drizzled/table_proto.h>

static bool shutdown_has_begun= false; // Once we put in the container for the vector/etc for engines this will go away.

using namespace std;

namespace drizzled
{

namespace plugin
{

static EngineVector vector_of_engines;

const std::string UNKNOWN_STRING("UNKNOWN");
const std::string DEFAULT_DEFINITION_FILE_EXT(".dfe");

static std::set<std::string> set_of_table_definition_ext;

StorageEngine::StorageEngine(const string name_arg,
                                     const bitset<HTON_BIT_SIZE> &flags_arg)
    : Plugin(name_arg, "StorageEngine"),
      enabled(true),
      flags(flags_arg),
      slot(0)
{
  if (enabled)
  {
    slot= total_ha++;
  }
  pthread_mutex_init(&proto_cache_mutex, NULL);
}

StorageEngine::~StorageEngine()
{
  pthread_mutex_destroy(&proto_cache_mutex);
}

void StorageEngine::setTransactionReadWrite(Session& session)
{
  TransactionContext &statement_ctx= session.transaction.stmt;
  statement_ctx.markModifiedNonTransData();
}

int StorageEngine::doRenameTable(Session *,
                                         const char *from,
                                         const char *to)
{
  int error= 0;
  for (const char **ext= bas_ext(); *ext ; ext++)
  {
    if (rename_file_ext(from, to, *ext))
    {
      if ((error=errno) != ENOENT)
	break;
      error= 0;
    }
  }
  return error;
}


/**
  Delete all files with extension from bas_ext().

  @param name		Base name of table

  @note
    We assume that the Cursor may return more extensions than
    was actually used for the file.

  @retval
    0   If we successfully deleted at least one file from base_ext and
    didn't get any other errors than ENOENT
  @retval
    !0  Error
*/
int StorageEngine::doDropTable(Session&,
                                       const string table_path)
{
  int error= 0;
  int enoent_or_zero= ENOENT;                   // Error if no file was deleted
  char buff[FN_REFLEN];

  for (const char **ext= bas_ext(); *ext ; ext++)
  {
    internal::fn_format(buff, table_path.c_str(), "", *ext,
              MY_UNPACK_FILENAME|MY_APPEND_EXT);
    if (internal::my_delete_with_symlink(buff, MYF(0)))
    {
      if ((error= errno) != ENOENT)
	break;
    }
    else
      enoent_or_zero= 0;                        // No error for ENOENT
    error= enoent_or_zero;
  }
  return error;
}

const char *StorageEngine::checkLowercaseNames(const char *path,
                                                       char *tmp_path)
{
  if (flags.test(HTON_BIT_FILE_BASED))
    return path;

  /* Ensure that table Cursor get path in lower case */
  if (tmp_path != path)
    strcpy(tmp_path, path);

  /*
    we only should turn into lowercase database/table part
    so start the process after homedirectory
  */
  if (strstr(tmp_path, drizzle_tmpdir) == tmp_path)
    my_casedn_str(files_charset_info, tmp_path + strlen(drizzle_tmpdir));
  else
    my_casedn_str(files_charset_info, tmp_path + drizzle_data_home_len);

  return tmp_path;
}


bool StorageEngine::addPlugin(StorageEngine *engine)
{

  vector_of_engines.push_back(engine);

  if (engine->getTableDefinitionFileExtension().length())
  {
    assert(engine->getTableDefinitionFileExtension().length() == DEFAULT_DEFINITION_FILE_EXT.length());
    set_of_table_definition_ext.insert(engine->getTableDefinitionFileExtension());
  }

  return false;
}

void StorageEngine::removePlugin(StorageEngine *)
{
  if (shutdown_has_begun == false)
  {
    vector_of_engines.clear();

    shutdown_has_begun= true;
  }
}

class FindEngineByName
  : public unary_function<StorageEngine *, bool>
{
  const string target;
public:
  explicit FindEngineByName(const string target_arg)
    : target(target_arg)
  {}
  result_type operator() (argument_type engine)
  {
    string engine_name(engine->getName());

    transform(engine_name.begin(), engine_name.end(),
              engine_name.begin(), ::tolower);
    return engine_name == target;
  }
};

StorageEngine *StorageEngine::findByName(string find_str)
{
  transform(find_str.begin(), find_str.end(),
            find_str.begin(), ::tolower);

  
  EngineVector::iterator iter= find_if(vector_of_engines.begin(),
                                       vector_of_engines.end(),
                                       FindEngineByName(find_str));
  if (iter != vector_of_engines.end())
  {
    StorageEngine *engine= *iter;
    if (engine->is_user_selectable())
      return engine;
  }

  return NULL;
}

StorageEngine *StorageEngine::findByName(Session& session,
                                                         string find_str)
{
  
  transform(find_str.begin(), find_str.end(),
            find_str.begin(), ::tolower);

  if (find_str.compare("default") == 0)
    return session.getDefaultStorageEngine();

  EngineVector::iterator iter= find_if(vector_of_engines.begin(),
                                       vector_of_engines.end(),
                                       FindEngineByName(find_str));
  if (iter != vector_of_engines.end())
  {
    StorageEngine *engine= *iter;
    if (engine->is_user_selectable())
      return engine;
  }

  return NULL;
}

class StorageEngineCloseConnection
: public unary_function<StorageEngine *, void>
{
  Session *session;
public:
  StorageEngineCloseConnection(Session *session_arg) : session(session_arg) {}
  /*
    there's no need to rollback here as all transactions must
    be rolled back already
  */
  inline result_type operator() (argument_type engine)
  {
    if (engine->is_enabled() && (*session->getEngineData(engine)))
      engine->close_connection(session);
  }
};

/**
  @note
    don't bother to rollback here, it's done already
*/
void StorageEngine::closeConnection(Session* session)
{
  for_each(vector_of_engines.begin(), vector_of_engines.end(),
           StorageEngineCloseConnection(session));
}

void StorageEngine::dropDatabase(char* path)
{
  for_each(vector_of_engines.begin(), vector_of_engines.end(),
           bind2nd(mem_fun(&StorageEngine::drop_database),path));
}

bool StorageEngine::flushLogs(StorageEngine *engine)
{
  if (engine == NULL)
  {
    if (find_if(vector_of_engines.begin(), vector_of_engines.end(),
            mem_fun(&StorageEngine::flush_logs))
          != vector_of_engines.begin())
      return true;
  }
  else
  {
    if ((!engine->is_enabled()) ||
        (engine->flush_logs()))
      return true;
  }
  return false;
}

class StorageEngineGetTableDefinition: public unary_function<StorageEngine *,bool>
{
  Session& session;
  const char* path;
  const char *db;
  const char *table_name;
  const bool is_tmp;
  message::Table *table_proto;
  int *err;

public:
  StorageEngineGetTableDefinition(Session& session_arg,
                                  const char* path_arg,
                                  const char *db_arg,
                                  const char *table_name_arg,
                                  const bool is_tmp_arg,
                                  message::Table *table_proto_arg,
                                  int *err_arg) :
    session(session_arg), 
    path(path_arg), 
    db(db_arg),
    table_name(table_name_arg),
    is_tmp(is_tmp_arg),
    table_proto(table_proto_arg), 
    err(err_arg) {}

  result_type operator() (argument_type engine)
  {
    int ret= engine->doGetTableDefinition(session,
                                          path, 
                                          db,
                                          table_name,
                                          is_tmp,
                                          table_proto);

    if (ret != ENOENT)
      *err= ret;

    return *err == EEXIST || *err != ENOENT;
  }
};

static int drizzle_read_table_proto(const char* path, message::Table* table)
{
  int fd= open(path, O_RDONLY);

  if (fd == -1)
    return errno;

  google::protobuf::io::ZeroCopyInputStream* input=
    new google::protobuf::io::FileInputStream(fd);

  if (table->ParseFromZeroCopyStream(input) == false)
  {
    delete input;
    close(fd);
    return -1;
  }

  delete input;
  close(fd);
  return 0;
}

/**
  Utility method which hides some of the details of getTableDefinition()
*/
bool plugin::StorageEngine::doesTableExist(Session& session,
                                           TableIdentifier &identifier,
                                           bool include_temporary_tables)
{
  return (plugin::StorageEngine::getTableDefinition(session, identifier, NULL, include_temporary_tables) == EEXIST);
}

/**
  Call this function in order to give the Cursor the possiblity
  to ask engine if there are any new tables that should be written to disk
  or any dropped tables that need to be removed from disk
*/
int StorageEngine::getTableDefinition(Session& session,
                                              TableIdentifier &identifier,
                                              message::Table *table_proto,
                                              bool include_temporary_tables)
{
  return getTableDefinition(session,
                            identifier.getPath(), identifier.getDBName(), identifier.getTableName(), identifier.isTmp(),
                            table_proto, include_temporary_tables);
}

int StorageEngine::getTableDefinition(Session& session,
                                              const char* path,
                                              const char *schema_name,
                                              const char *table_name,
                                              const bool,
                                              message::Table *table_proto,
                                              bool include_temporary_tables)
{
  int err= ENOENT;

  if (include_temporary_tables)
  {
    if (session.doGetTableDefinition(path, schema_name, table_name, false, table_proto) == EEXIST)
      return EEXIST;
  }

  vector<StorageEngine *>::iterator iter=
    find_if(vector_of_engines.begin(), vector_of_engines.end(),
            StorageEngineGetTableDefinition(session, path, NULL, NULL, true, table_proto, &err));

  if (iter == vector_of_engines.end())
  {
    string proto_path(path);
    string file_ext(".dfe");
    proto_path.append(file_ext);

    int error= access(proto_path.c_str(), F_OK);

    if (error == 0)
      err= EEXIST;
    else
      err= errno;

    if (table_proto)
    {
      int read_proto_err= drizzle_read_table_proto(proto_path.c_str(),
                                                   table_proto);

      if (read_proto_err)
        err= read_proto_err;
    }
  }

  return err;
}

/**
  An interceptor to hijack the text of the error message without
  setting an error in the thread. We need the text to present it
  in the form of a warning to the user.
*/

class Ha_delete_table_error_handler: public Internal_error_handler
{
public:
  Ha_delete_table_error_handler() : Internal_error_handler() {}
  virtual bool handle_error(uint32_t sql_errno,
                            const char *message,
                            DRIZZLE_ERROR::enum_warning_level level,
                            Session *session);
  char buff[DRIZZLE_ERRMSG_SIZE];
};


bool
Ha_delete_table_error_handler::
handle_error(uint32_t ,
             const char *message,
             DRIZZLE_ERROR::enum_warning_level ,
             Session *)
{
  /* Grab the error message */
  strncpy(buff, message, sizeof(buff)-1);
  return true;
}


/**
   returns ENOENT if the file doesn't exists.
*/
int StorageEngine::dropTable(Session& session,
                                     TableIdentifier &identifier,
                                     bool generate_warning)
{
  int error= 0;
  int error_proto;
  message::Table src_proto;
  StorageEngine* engine;

  error_proto= StorageEngine::getTableDefinition(session,
                                                         identifier,
                                                         &src_proto);

  if (error_proto == ER_CORRUPT_TABLE_DEFINITION)
  {
    my_error(ER_CORRUPT_TABLE_DEFINITION, MYF(0),
             src_proto.InitializationErrorString().c_str());
    return ER_CORRUPT_TABLE_DEFINITION;
  }

  engine= StorageEngine::findByName(session, src_proto.engine().name());

  if (engine)
  {
    engine->setTransactionReadWrite(session);
    error= engine->doDropTable(session, identifier.getPath());
  }

  if (error != ENOENT)
  {
    if (error == 0)
    {
      if (engine && engine->check_flag(HTON_BIT_HAS_DATA_DICTIONARY))
      {
        deleteDefinitionFromPath(identifier);
      }
      else
      {
        error= deleteDefinitionFromPath(identifier);
      }
    }
  }

  if (error_proto && error == 0)
    return 0;

  if (((error_proto != EEXIST && error_proto != ENOENT)
      && !engine && generate_warning)
      | ( error && !engine && generate_warning))
  {
    my_error(ER_GET_ERRNO, MYF(0), error_proto);
    return error_proto;
  }

  if (error && generate_warning)
  {
    /*
      Because engine->print_error() use my_error() to generate the error message
      we use an internal error Cursor to intercept it and store the text
      in a temporary buffer. Later the message will be presented to user
      as a warning.
    */
    Ha_delete_table_error_handler ha_delete_table_error_handler;

    session.push_internal_handler(&ha_delete_table_error_handler);
    engine->print_error(error, 0);

    session.pop_internal_handler();

    /*
      XXX: should we convert *all* errors to warnings here?
      What if the error is fatal?
    */
    push_warning(&session, DRIZZLE_ERROR::WARN_LEVEL_ERROR, error,
                 ha_delete_table_error_handler.buff);
  }

  return error;
}

/**
  Initiates table-file and calls appropriate database-creator.

  @retval
   0  ok
  @retval
   1  error

   @todo refactor to remove goto
*/
int StorageEngine::createTable(Session& session,
                                       TableIdentifier &identifier,
                                       bool update_create_info,
                                       message::Table& table_proto, bool proto_used)
{
  int error= 1;
  Table table;
  TableShare share(identifier.getDBName(), 0, identifier.getTableName(), identifier.getPath());
  message::Table tmp_proto;

  if (proto_used)
  {
    if (parse_table_proto(session, table_proto, &share))
      goto err;
  }
  else
  {
    if (open_table_def(session, &share))
      goto err;
  }

  if (open_table_from_share(&session, &share, "", 0, 0,
                            &table))
    goto err;

  if (update_create_info)
    table.updateCreateInfo(&table_proto);

  /* Check for legal operations against the Engine using the proto (if used) */
  if (proto_used)
  {
    if (table_proto.type() == message::Table::TEMPORARY &&
        share.storage_engine->check_flag(HTON_BIT_TEMPORARY_NOT_SUPPORTED) == true)
    {
      error= HA_ERR_UNSUPPORTED;
      goto err2;
    }
    else if (table_proto.type() != message::Table::TEMPORARY &&
             share.storage_engine->check_flag(HTON_BIT_TEMPORARY_ONLY) == true)
    {
      error= HA_ERR_UNSUPPORTED;
      goto err2;
    }
  }

  if (! share.storage_engine->is_enabled())
  {
    error= HA_ERR_UNSUPPORTED;
    goto err2;
  }


  {
    char name_buff[FN_REFLEN];
    const char *table_name_arg;

    table_name_arg= share.storage_engine->checkLowercaseNames(identifier.getPath(), name_buff);

    share.storage_engine->setTransactionReadWrite(session);

    error= share.storage_engine->doCreateTable(&session,
                                               table_name_arg,
                                               table,
                                               table_proto);
  }

err2:
  table.closefrm(false);

  if (error)
  {
    char name_buff[FN_REFLEN];
    sprintf(name_buff,"%s.%s", identifier.getDBName(), identifier.getTableName());
    my_error(ER_CANT_CREATE_TABLE, MYF(ME_BELL+ME_WAITTANG), name_buff, error);
  }
err:
  share.free_table_share();
  return(error != 0);
}

Cursor *StorageEngine::getCursor(TableShare &share, memory::Root *alloc)
{
  assert(enabled);
  return create(share, alloc);
}

/**
  TODO -> Remove this to force all engines to implement their own file. Solves the "we only looked at dfe" problem.
*/
void StorageEngine::doGetTableNames(CachedDirectory &directory, string&, set<string>& set_of_names)
{
  CachedDirectory::Entries entries= directory.getEntries();

  for (CachedDirectory::Entries::iterator entry_iter= entries.begin(); 
       entry_iter != entries.end(); ++entry_iter)
  {
    CachedDirectory::Entry *entry= *entry_iter;
    const string *filename= &entry->filename;

    assert(filename->size());

    const char *ext= strchr(filename->c_str(), '.');

    if (ext == NULL || my_strcasecmp(system_charset_info, ext, DEFAULT_DEFINITION_FILE_EXT.c_str()) ||
        (filename->compare(0, strlen(TMP_FILE_PREFIX), TMP_FILE_PREFIX) == 0))
    { }
    else
    {
      char uname[NAME_LEN + 1];
      uint32_t file_name_len;

      file_name_len= filename_to_tablename(filename->c_str(), uname, sizeof(uname));
      // TODO: Remove need for memory copy here
      uname[file_name_len - sizeof(".dfe") + 1]= '\0'; // Subtract ending, place NULL 
      set_of_names.insert(uname);
    }
  }
}

class AddTableName : 
  public unary_function<StorageEngine *, void>
{
  string db;
  CachedDirectory& directory;
  set<string>& set_of_names;

public:

  AddTableName(CachedDirectory& directory_arg, const string& database_name, set<string>& of_names) :
    directory(directory_arg),
    set_of_names(of_names)
  {
    db= database_name;
  }

  result_type operator() (argument_type engine)
  {
    engine->doGetTableNames(directory, db, set_of_names);
  }
};

class AddSchemaNames : 
  public unary_function<StorageEngine *, void>
{
  set<string>& set_of_names;

public:

  AddSchemaNames(set<string>& of_names) :
    set_of_names(of_names)
  {
  }

  result_type operator() (argument_type engine)
  {
    engine->doGetSchemaNames(set_of_names);
  }
};

void StorageEngine::getSchemaNames(set<string>& set_of_names)
{
  // Add hook here for engines to register schema.
  for_each(vector_of_engines.begin(), vector_of_engines.end(),
           AddSchemaNames(set_of_names));
}

class StorageEngineGetSchemaDefinition: public unary_function<StorageEngine *, bool>
{
  const std::string &schema_name;
  message::Schema &schema_proto;

public:
  StorageEngineGetSchemaDefinition(const std::string &schema_name_arg,
                                  message::Schema &schema_proto_arg) :
    schema_name(schema_name_arg),
    schema_proto(schema_proto_arg) 
  { }

  result_type operator() (argument_type engine)
  {
    return engine->doGetSchemaDefinition(schema_name, schema_proto);
  }
};

/*
  Return value is "if parsed"
*/
bool StorageEngine::getSchemaDefinition(const std::string &schema_name, message::Schema &proto)
{
  proto.Clear();

  vector<StorageEngine *>::iterator iter=
    find_if(vector_of_engines.begin(), vector_of_engines.end(),
            StorageEngineGetSchemaDefinition(schema_name, proto));

  if (iter != vector_of_engines.end())
  {
    return true;
  }

  return false;
}


const CHARSET_INFO *StorageEngine::getSchemaCollation(const std::string &schema_name)
{
  message::Schema schmema_proto;
  bool found;

  found= StorageEngine::getSchemaDefinition(schema_name, schmema_proto);

  if (found && schmema_proto.has_collation())
  {
    const string buffer= schmema_proto.collation();
    const CHARSET_INFO* cs= get_charset_by_name(buffer.c_str());

    if (not cs)
    {
      errmsg_printf(ERRMSG_LVL_ERROR,
                    _("Error while loading database options: '%s':"), schema_name.c_str());
      errmsg_printf(ERRMSG_LVL_ERROR, ER(ER_UNKNOWN_COLLATION), buffer.c_str());

      return default_charset_info;
    }

    return cs;
  }

  return default_charset_info;
}

void StorageEngine::getTableNames(const string& db, set<string>& set_of_names)
{
  char tmp_path[FN_REFLEN];

  build_table_filename(tmp_path, sizeof(tmp_path), db.c_str(), "", false);

  CachedDirectory directory(tmp_path, set_of_table_definition_ext);

  if (db.compare("information_schema"))
  {
    if (directory.fail())
    {
      errno= directory.getError();
      if (errno == ENOENT)
        my_error(ER_BAD_DB_ERROR, MYF(ME_BELL+ME_WAITTANG), db.c_str());
      else
        my_error(ER_CANT_READ_DIR, MYF(ME_BELL+ME_WAITTANG), directory.getPath(), errno);
      return;
    }
  }

  for_each(vector_of_engines.begin(), vector_of_engines.end(),
           AddTableName(directory, db, set_of_names));

  Session *session= current_session;

  session->doGetTableNames(directory, db, set_of_names);

}

/* This will later be converted to TableIdentifiers */
class DropTables: public unary_function<StorageEngine *, void>
{
  Session &session;
  set<string>& set_of_names;

public:

  DropTables(Session &session_arg, set<string>& of_names) :
    session(session_arg),
    set_of_names(of_names)
  { }

  result_type operator() (argument_type engine)
  {

    for (set<string>::iterator iter= set_of_names.begin();
         iter != set_of_names.end();
         iter++)
    {
      int error= engine->doDropTable(session, *iter);

      // On a return of zero we know we found and deleted the table. So we
      // remove it from our search.
      if (! error)
        set_of_names.erase(iter);
    }
  }
};

/*
  This only works for engines which use file based DFE.

  Note-> Unlike MySQL, we do not, on purpose, delete files that do not match any engines. 
*/
void StorageEngine::removeLostTemporaryTables(Session &session, const char *directory)
{
  CachedDirectory dir(directory, set_of_table_definition_ext);
  set<string> set_of_table_names;

  if (dir.fail())
  {
    errno= dir.getError();
    my_error(ER_CANT_READ_DIR, MYF(0), directory, errno);

    return;
  }

  CachedDirectory::Entries files= dir.getEntries();

  for (CachedDirectory::Entries::iterator fileIter= files.begin();
       fileIter != files.end(); fileIter++)
  {
    size_t length;
    string path;
    CachedDirectory::Entry *entry= *fileIter;

    /* We remove the file extension. */
    length= entry->filename.length();
    entry->filename.resize(length - DEFAULT_DEFINITION_FILE_EXT.length());

    path+= directory;
    path+= FN_LIBCHAR;
    path+= entry->filename;
    set_of_table_names.insert(path);
  }

  for_each(vector_of_engines.begin(), vector_of_engines.end(),
           DropTables(session, set_of_table_names));
  
  /*
    Now we just clean up anything that might left over.

    We rescan because some of what might have been there should
    now be all nice and cleaned up.
  */
  set<string> all_exts= set_of_table_definition_ext;

  for (vector<StorageEngine *>::iterator iter= vector_of_engines.begin();
       iter != vector_of_engines.end() ; iter++)
  {
    for (const char **ext= (*iter)->bas_ext(); *ext ; ext++)
      all_exts.insert(*ext);
  }

  CachedDirectory rescan(directory, all_exts);

  files= rescan.getEntries();
  for (CachedDirectory::Entries::iterator fileIter= files.begin();
       fileIter != files.end(); fileIter++)
  {
    string path;
    CachedDirectory::Entry *entry= *fileIter;

    path+= directory;
    path+= FN_LIBCHAR;
    path+= entry->filename;

    unlink(path.c_str());
  }
}


/**
  Print error that we got from Cursor function.

  @note
    In case of delete table it's only safe to use the following parts of
    the 'table' structure:
    - table->s->path
    - table->alias
*/
void StorageEngine::print_error(int error, myf errflag, Table &table)
{
  print_error(error, errflag, &table);
}

void StorageEngine::print_error(int error, myf errflag, Table *table)
{
  int textno= ER_GET_ERRNO;
  switch (error) {
  case EACCES:
    textno=ER_OPEN_AS_READONLY;
    break;
  case EAGAIN:
    textno=ER_FILE_USED;
    break;
  case ENOENT:
    textno=ER_FILE_NOT_FOUND;
    break;
  case HA_ERR_KEY_NOT_FOUND:
  case HA_ERR_NO_ACTIVE_RECORD:
  case HA_ERR_END_OF_FILE:
    textno=ER_KEY_NOT_FOUND;
    break;
  case HA_ERR_WRONG_MRG_TABLE_DEF:
    textno=ER_WRONG_MRG_TABLE;
    break;
  case HA_ERR_FOUND_DUPP_KEY:
  {
    assert(table);
    uint32_t key_nr= table->get_dup_key(error);
    if ((int) key_nr >= 0)
    {
      const char *err_msg= ER(ER_DUP_ENTRY_WITH_KEY_NAME);

      if (key_nr == 0 &&
          (table->key_info[0].key_part[0].field->flags &
           AUTO_INCREMENT_FLAG)
          && (current_session)->lex->sql_command == SQLCOM_ALTER_TABLE)
      {
        err_msg= ER(ER_DUP_ENTRY_AUTOINCREMENT_CASE);
      }

      print_keydup_error(key_nr, err_msg, *table);
      return;
    }
    textno=ER_DUP_KEY;
    break;
  }
  case HA_ERR_FOREIGN_DUPLICATE_KEY:
  {
    assert(table);
    uint32_t key_nr= table->get_dup_key(error);
    if ((int) key_nr >= 0)
    {
      uint32_t max_length;

      /* Write the key in the error message */
      char key[MAX_KEY_LENGTH];
      String str(key,sizeof(key),system_charset_info);

      /* Table is opened and defined at this point */
      key_unpack(&str,table,(uint32_t) key_nr);
      max_length= (DRIZZLE_ERRMSG_SIZE-
                   (uint32_t) strlen(ER(ER_FOREIGN_DUPLICATE_KEY)));
      if (str.length() >= max_length)
      {
        str.length(max_length-4);
        str.append(STRING_WITH_LEN("..."));
      }
      my_error(ER_FOREIGN_DUPLICATE_KEY, MYF(0), table->s->table_name.str,
        str.c_ptr(), key_nr+1);
      return;
    }
    textno= ER_DUP_KEY;
    break;
  }
  case HA_ERR_FOUND_DUPP_UNIQUE:
    textno=ER_DUP_UNIQUE;
    break;
  case HA_ERR_RECORD_CHANGED:
    textno=ER_CHECKREAD;
    break;
  case HA_ERR_CRASHED:
    textno=ER_NOT_KEYFILE;
    break;
  case HA_ERR_WRONG_IN_RECORD:
    textno= ER_CRASHED_ON_USAGE;
    break;
  case HA_ERR_CRASHED_ON_USAGE:
    textno=ER_CRASHED_ON_USAGE;
    break;
  case HA_ERR_NOT_A_TABLE:
    textno= error;
    break;
  case HA_ERR_CRASHED_ON_REPAIR:
    textno=ER_CRASHED_ON_REPAIR;
    break;
  case HA_ERR_OUT_OF_MEM:
    textno=ER_OUT_OF_RESOURCES;
    break;
  case HA_ERR_WRONG_COMMAND:
    textno=ER_ILLEGAL_HA;
    break;
  case HA_ERR_OLD_FILE:
    textno=ER_OLD_KEYFILE;
    break;
  case HA_ERR_UNSUPPORTED:
    textno=ER_UNSUPPORTED_EXTENSION;
    break;
  case HA_ERR_RECORD_FILE_FULL:
  case HA_ERR_INDEX_FILE_FULL:
    textno=ER_RECORD_FILE_FULL;
    break;
  case HA_ERR_LOCK_WAIT_TIMEOUT:
    textno=ER_LOCK_WAIT_TIMEOUT;
    break;
  case HA_ERR_LOCK_TABLE_FULL:
    textno=ER_LOCK_TABLE_FULL;
    break;
  case HA_ERR_LOCK_DEADLOCK:
    textno=ER_LOCK_DEADLOCK;
    break;
  case HA_ERR_READ_ONLY_TRANSACTION:
    textno=ER_READ_ONLY_TRANSACTION;
    break;
  case HA_ERR_CANNOT_ADD_FOREIGN:
    textno=ER_CANNOT_ADD_FOREIGN;
    break;
  case HA_ERR_ROW_IS_REFERENCED:
  {
    String str;
    get_error_message(error, &str);
    my_error(ER_ROW_IS_REFERENCED_2, MYF(0), str.c_ptr_safe());
    return;
  }
  case HA_ERR_NO_REFERENCED_ROW:
  {
    String str;
    get_error_message(error, &str);
    my_error(ER_NO_REFERENCED_ROW_2, MYF(0), str.c_ptr_safe());
    return;
  }
  case HA_ERR_TABLE_DEF_CHANGED:
    textno=ER_TABLE_DEF_CHANGED;
    break;
  case HA_ERR_NO_SUCH_TABLE:
    assert(table);
    my_error(ER_NO_SUCH_TABLE, MYF(0), table->s->db.str,
             table->s->table_name.str);
    return;
  case HA_ERR_RBR_LOGGING_FAILED:
    textno= ER_BINLOG_ROW_LOGGING_FAILED;
    break;
  case HA_ERR_DROP_INDEX_FK:
  {
    assert(table);
    const char *ptr= "???";
    uint32_t key_nr= table->get_dup_key(error);
    if ((int) key_nr >= 0)
      ptr= table->key_info[key_nr].name;
    my_error(ER_DROP_INDEX_FK, MYF(0), ptr);
    return;
  }
  case HA_ERR_TABLE_NEEDS_UPGRADE:
    textno=ER_TABLE_NEEDS_UPGRADE;
    break;
  case HA_ERR_TABLE_READONLY:
    textno= ER_OPEN_AS_READONLY;
    break;
  case HA_ERR_AUTOINC_READ_FAILED:
    textno= ER_AUTOINC_READ_FAILED;
    break;
  case HA_ERR_AUTOINC_ERANGE:
    textno= ER_WARN_DATA_OUT_OF_RANGE;
    break;
  case HA_ERR_LOCK_OR_ACTIVE_TRANSACTION:
    my_message(ER_LOCK_OR_ACTIVE_TRANSACTION,
               ER(ER_LOCK_OR_ACTIVE_TRANSACTION), MYF(0));
    return;
  default:
    {
      /* 
        The error was "unknown" to this function.
        Ask Cursor if it has got a message for this error 
      */
      bool temporary= false;
      String str;
      temporary= get_error_message(error, &str);
      if (!str.is_empty())
      {
        const char* engine_name= getName().c_str();
        if (temporary)
          my_error(ER_GET_TEMPORARY_ERRMSG, MYF(0), error, str.ptr(),
                   engine_name);
        else
          my_error(ER_GET_ERRMSG, MYF(0), error, str.ptr(), engine_name);
      }
      else
      {
	      my_error(ER_GET_ERRNO,errflag,error);
      }
      return;
    }
  }
  my_error(textno, errflag, table->s->table_name.str, error);
}


/**
  Return an error message specific to this Cursor.

  @param error  error code previously returned by Cursor
  @param buf    pointer to String where to add error message

  @return
    Returns true if this is a temporary error
*/
bool StorageEngine::get_error_message(int , String* )
{
  return false;
}


void StorageEngine::print_keydup_error(uint32_t key_nr, const char *msg, Table &table)
{
  /* Write the duplicated key in the error message */
  char key[MAX_KEY_LENGTH];
  String str(key,sizeof(key),system_charset_info);

  if (key_nr == MAX_KEY)
  {
    /* Key is unknown */
    str.copy("", 0, system_charset_info);
    my_printf_error(ER_DUP_ENTRY, msg, MYF(0), str.c_ptr(), "*UNKNOWN*");
  }
  else
  {
    /* Table is opened and defined at this point */
    key_unpack(&str, &table, (uint32_t) key_nr);
    uint32_t max_length=DRIZZLE_ERRMSG_SIZE-(uint32_t) strlen(msg);
    if (str.length() >= max_length)
    {
      str.length(max_length-4);
      str.append(STRING_WITH_LEN("..."));
    }
    my_printf_error(ER_DUP_ENTRY, msg,
		    MYF(0), str.c_ptr(), table.key_info[key_nr].name);
  }
}


int StorageEngine::deleteDefinitionFromPath(TableIdentifier &identifier)
{
  string path(identifier.getPath());

  path.append(DEFAULT_DEFINITION_FILE_EXT);

  return internal::my_delete(path.c_str(), MYF(0));
}

int StorageEngine::renameDefinitionFromPath(TableIdentifier &dest, TableIdentifier &src)
{
  string src_path(src.getPath());
  string dest_path(dest.getPath());

  src_path.append(DEFAULT_DEFINITION_FILE_EXT);
  dest_path.append(DEFAULT_DEFINITION_FILE_EXT);

  return internal::my_rename(src_path.c_str(), dest_path.c_str(), MYF(MY_WME));
}

int StorageEngine::writeDefinitionFromPath(TableIdentifier &identifier, message::Table &table_proto)
{
  string file_name(identifier.getPath());

  file_name.append(DEFAULT_DEFINITION_FILE_EXT);

  int fd= open(file_name.c_str(), O_RDWR|O_CREAT|O_TRUNC, internal::my_umask);

  if (fd == -1)
    return errno;

  google::protobuf::io::ZeroCopyOutputStream* output=
    new google::protobuf::io::FileOutputStream(fd);

  if (table_proto.SerializeToZeroCopyStream(output) == false)
  {
    delete output;
    close(fd);
    return errno;
  }

  delete output;
  close(fd);
  return 0;
}

} /* namespace plugin */
} /* namespace drizzled */
