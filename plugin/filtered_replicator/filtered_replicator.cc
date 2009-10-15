/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
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

/**
 * @file
 *
 * Defines the implementation of a simple replicator that can filter
 * events based on a schema or table name.
 *
 * @details
 *
 * This is a very simple implementation.  All we do is maintain two
 * std::vectors:
 *
 *  1) contains all the schema names to filter
 *  2) contains all the table names to filter
 *
 * If an event is on a schema or table in the vectors described above, then
 * the event will not be passed along to the applier.
 */

#include "filtered_replicator.h"

#include <drizzled/gettext.h>
#include <drizzled/plugin/transaction_applier.h>
#include <drizzled/message/transaction.pb.h>

#include <vector>
#include <string>

using namespace std;
using namespace drizzled;

static bool sysvar_filtered_replicator_enabled= false;
static char *sysvar_filtered_replicator_sch_filters= NULL;
static char *sysvar_filtered_replicator_tab_filters= NULL;
static char *sysvar_filtered_replicator_sch_regex= NULL;
static char *sysvar_filtered_replicator_tab_regex= NULL;

FilteredReplicator::FilteredReplicator(string name_arg,
                                       const char *in_sch_filters,
                                       const char *in_tab_filters)
  :
    plugin::TransactionReplicator(name_arg),
    schemas_to_filter(),
    tables_to_filter(),
    sch_filter_string(in_sch_filters),
    tab_filter_string(in_tab_filters),
    sch_regex_enabled(false),
    tab_regex_enabled(false),
    sch_re(NULL),
    tab_re(NULL)
{
  /* 
   * Add each of the specified schemas to the vector of schemas
   * to filter.
   */
  if (in_sch_filters)
  {
    populateFilter(sch_filter_string, schemas_to_filter);
  }

  /* 
   * Add each of the specified tables to the vector of tables
   * to filter.
   */
  if (in_tab_filters)
  {
    populateFilter(tab_filter_string, tables_to_filter);
  }

  /* 
   * Compile the regular expression for schema's to filter
   * if one is specified.
   */
  if (sysvar_filtered_replicator_sch_regex)
  {
    const char *error= NULL;
    int32_t error_offset= 0;
    sch_re= pcre_compile(sysvar_filtered_replicator_sch_regex,
                         0,
                         &error,
                         &error_offset,
                         NULL);
    sch_regex_enabled= true;
  }

  /* 
   * Compile the regular expression for table's to filter
   * if one is specified.
   */
  if (sysvar_filtered_replicator_tab_regex)
  {
    const char *error= NULL;
    int32_t error_offset= 0;
    tab_re= pcre_compile(sysvar_filtered_replicator_tab_regex,
                         0,
                         &error,
                         &error_offset,
                         NULL);
    tab_regex_enabled= true;
  }

  pthread_mutex_init(&sch_vector_lock, NULL);
  pthread_mutex_init(&tab_vector_lock, NULL);
  pthread_mutex_init(&sysvar_sch_lock, NULL);
  pthread_mutex_init(&sysvar_tab_lock, NULL);
}

bool FilteredReplicator::isActive() const
{
  return sysvar_filtered_replicator_enabled;
}

void FilteredReplicator::parseStatementTableMetadata(const message::Statement &in_statement,
                                                     string &in_schema_name,
                                                     string &in_table_name) const
{
  switch (in_statement.type())
  {
    case message::Statement::INSERT:
    {
      const message::TableMetadata &metadata= in_statement.insert_header().table_metadata();
      in_schema_name.assign(metadata.schema_name());
      in_table_name.assign(metadata.table_name());
      break;
    }
    case message::Statement::UPDATE:
    {
      const message::TableMetadata &metadata= in_statement.update_header().table_metadata();
      in_schema_name.assign(metadata.schema_name());
      in_table_name.assign(metadata.table_name());
      break;
    }
    case message::Statement::DELETE:
    {
      const message::TableMetadata &metadata= in_statement.delete_header().table_metadata();
      in_schema_name.assign(metadata.schema_name());
      in_table_name.assign(metadata.table_name());
      break;
    }
    case message::Statement::CREATE_SCHEMA:
    {
      in_schema_name.assign(in_statement.create_schema_statement().schema().name());
      in_table_name.clear();
      break;
    }
    case message::Statement::ALTER_SCHEMA:
    {
      in_schema_name.assign(in_statement.alter_schema_statement().after().name());
      in_table_name.clear();
      break;
    }
    case message::Statement::DROP_SCHEMA:
    {
      in_schema_name.assign(in_statement.drop_schema_statement().schema_name());
      in_table_name.clear();
      break;
    }
    case message::Statement::CREATE_TABLE:
    {
      // in_schema_name.assign(in_statement.create_table_statement().table().name());
      in_table_name.assign(in_statement.create_table_statement().table().name());
      break;
    }
    case message::Statement::ALTER_TABLE:
    {
      // in_schema_name.assign(in_statement.alter_table_statement().table().name());
      in_table_name.assign(in_statement.alter_table_statement().after().name());
      break;
    }
    case message::Statement::DROP_TABLE:
    {
      const message::TableMetadata &metadata= in_statement.drop_table_statement().table_metadata();
      in_schema_name.assign(metadata.schema_name());
      in_table_name.assign(metadata.table_name());
      break;
    }
    default:
    {
      /* All other types have no schema and table information */
      in_schema_name.clear();
      in_table_name.clear();
      break;
    }
  }  
}

void FilteredReplicator::activate()
{
  sysvar_filtered_replicator_enabled= true;
}

void FilteredReplicator::deactivate()
{
  sysvar_filtered_replicator_enabled= false;
}

void FilteredReplicator::replicate(plugin::TransactionApplier *in_applier, 
                                   message::Transaction &to_replicate)
{
  string schema_name;
  string table_name;

  size_t num_statements= to_replicate.statement_size();
  size_t x;

  /* 
   * We build a new transaction message containing only Statement
   * messages that have not been filtered.
   *
   * @todo A more efficient method would be to rework the pointers
   * that the to_replicate.statement() vector contains and remove
   * the statement pointers that are filtered...
   */
  message::Transaction filtered_transaction;

  for (x= 0; x < num_statements; ++x)
  {
    schema_name.clear();
    table_name.clear();

    const message::Statement &statement= to_replicate.statement(x);

    /*
     * First, we check to see if the command consists of raw SQL. If so,
     * we need to parse this SQL and determine whether to filter the event
     * based on the information we obtain from the parsed SQL.
     * If not raw SQL, check if this event should be filtered or not
     * based on the schema and table names in the command message.
     *
     * The schema and table names are stored in TableMetadata headers
     * for most types of Statement messages.
     */
    if (statement.type() == message::Statement::RAW_SQL)
    {
      parseQuery(statement.sql(), schema_name, table_name);
    }
    else
    {
      parseStatementTableMetadata(statement, schema_name, table_name);
    }

    /*
     * Convert the schema name and table name strings to lowercase so that it
     * does not matter what case the table or schema name was specified in. We
     * also keep all entries in the vectors of schemas and tables to filter in
     * lowercase.
     */
    std::transform(schema_name.begin(), schema_name.end(),
                  schema_name.begin(), ::tolower);
    std::transform(table_name.begin(), table_name.end(),
                  table_name.begin(), ::tolower);

    if (! isSchemaFiltered(schema_name) &&
        ! isTableFiltered(table_name))
    {
      message::Statement *s= filtered_transaction.add_statement();
      *s= statement; /* copy contruct */
    }
  }

  if (filtered_transaction.statement_size() > 0)
  {

    /*
     * We can now simply call the applier's apply() method, passing
     * along the supplied command.
     */
    message::TransactionContext *tc= filtered_transaction.mutable_transaction_context();
    *tc= to_replicate.transaction_context(); /* copy construct */
    in_applier->apply(filtered_transaction);
  }
}

void FilteredReplicator::populateFilter(std::string input,
                                        vector<string> &filter)
{
  /*
   * Convert the input string to lowercase so that all entries in the vector
   * will be in lowercase.
   */
  std::transform(input.begin(), input.end(),
                 input.begin(), ::tolower);
  string::size_type last_pos= input.find_first_not_of(',', 0);
  string::size_type pos= input.find_first_of(',', last_pos);

  while (pos != string::npos || last_pos != string::npos)
  {
    filter.push_back(input.substr(last_pos, pos - last_pos));
    last_pos= input.find_first_not_of(',', pos);
    pos= input.find_first_of(',', last_pos);
  }
}

bool FilteredReplicator::isSchemaFiltered(const string &schema_name)
{
  pthread_mutex_lock(&sch_vector_lock);
  vector<string>::iterator it= find(schemas_to_filter.begin(),
                                    schemas_to_filter.end(),
                                    schema_name);
  if (it != schemas_to_filter.end())
  {
    pthread_mutex_unlock(&sch_vector_lock);
    return true;
  }
  pthread_mutex_unlock(&sch_vector_lock);

  /* 
   * If regular expression matching is enabled for schemas to filter, then
   * we check to see if this schema name matches the regular expression that
   * has been specified. 
   */
  if (sch_regex_enabled)
  {
    int32_t result= pcre_exec(sch_re,
                              NULL,
                              schema_name.c_str(),
                              schema_name.length(),
                              0,
                              0,
                              NULL,
                              0);
    if (result >= 0)
    {
      return true;
    }
  }

  return false;
}

bool FilteredReplicator::isTableFiltered(const string &table_name)
{
  pthread_mutex_lock(&tab_vector_lock);
  vector<string>::iterator it= find(tables_to_filter.begin(),
                                    tables_to_filter.end(),
                                    table_name);
  if (it != tables_to_filter.end())
  {
    pthread_mutex_unlock(&tab_vector_lock);
    return true;
  }
  pthread_mutex_unlock(&tab_vector_lock);

  /* 
   * If regular expression matching is enabled for tables to filter, then
   * we check to see if this table name matches the regular expression that
   * has been specified. 
   */
  if (tab_regex_enabled)
  {
    int32_t result= pcre_exec(tab_re,
                              NULL,
                              table_name.c_str(),
                              table_name.length(),
                              0,
                              0,
                              NULL,
                              0);
    if (result >= 0)
    {
      return true;
    }
  }

  return false;
}

void FilteredReplicator::parseQuery(const string &sql,
                                    string &schema_name,
                                    string &table_name)
{
  /*
   * Determine what type of SQL we are dealing with e.g. create table,
   * drop table, etc.
   */
  string::size_type pos= sql.find_first_of(' ', 0);
  string type= sql.substr(0, pos);

  /*
   * Convert the type string to uppercase here so that it doesn't
   * matter what case the user entered the statement in.
   */
  std::transform(type.begin(), type.end(),
                 type.begin(), ::toupper);

  if (type.compare("DROP") == 0)
  {
    /*
     * The schema and table name can be either the third word
     * or the fifth word in a DROP TABLE statement...so we extract
     * the third word from the SQL and see whether it is and IF or
     * not.
     */
    pos= sql.find_first_of(' ', 11);
    string cmp_str= sql.substr(11, pos - 11);
    string target_name("");
    if (cmp_str.compare("IF") == 0)
    {
      /* the name must be the fifth word */
      pos= sql.find_first_of(' ', 21);
      target_name.assign(sql.substr(21, pos - 21));
    }
    else
    {
      target_name.assign(cmp_str);
    }
    /*
     * Determine whether the name is a concatenation of the schema
     * name and table name i.e. schema.table or just the table name
     * on its own.
     */
    pos= target_name.find_first_of('.', 0);
    if (pos != string::npos)
    {
      /*
       * There is a schema name here...
       */
      schema_name.assign(target_name.substr(0, pos));
      /*
       * The rest of the name string is the table name.
       */
      table_name.assign(target_name.substr(pos + 1));
    }
    else
    {
      table_name.assign(target_name);
    }
  }
  else if (type.compare("CREATE") == 0)
  {
    /*
     * The schema and table name are always the third word
     * in a CREATE TABLE statement...always (unless there is
     * some crazy syntax I am unaware of).
     */
    pos= sql.find_first_of(' ', 13);
    string target_name= sql.substr(13, pos - 13);
    /*
     * Determine whether the name is a concatenation of the schema
     * name and table name i.e. schema.table or just the table name
     * on its own.
     */
    pos= target_name.find_first_of('.', 0);
    if (pos != string::npos)
    {
      /*
       * There is a schema name here...
       */
      schema_name.assign(target_name.substr(0, pos));
      /*
       * The rest of the name string is the table name.
       */
      table_name.assign(target_name.substr(pos + 1));
    }
    else
    {
      table_name.assign(target_name);
    }
  }
  else
  {
    /* we only deal with DROP and CREATE table for the moment */
    return;
  }
}

void FilteredReplicator::setSchemaFilter(const string &input)
{
  pthread_mutex_lock(&sch_vector_lock);
  pthread_mutex_lock(&sysvar_sch_lock);
  sch_filter_string.assign(input);
  schemas_to_filter.clear();
  populateFilter(sch_filter_string, schemas_to_filter);
  pthread_mutex_unlock(&sch_vector_lock);
}

void FilteredReplicator::setTableFilter(const string &input)
{
  pthread_mutex_lock(&tab_vector_lock);
  pthread_mutex_lock(&sysvar_tab_lock);
  tab_filter_string.assign(input);
  tables_to_filter.clear();
  populateFilter(tab_filter_string, tables_to_filter);
  pthread_mutex_unlock(&tab_vector_lock);
}

static FilteredReplicator *filtered_replicator= NULL; /* The singleton replicator */

static int init(plugin::Registry &registry)
{
  if (sysvar_filtered_replicator_enabled)
  {
    filtered_replicator= new(std::nothrow) 
      FilteredReplicator("filtered_replicator",
                         sysvar_filtered_replicator_sch_filters,
                         sysvar_filtered_replicator_tab_filters);
    if (filtered_replicator == NULL)
    {
      return 1;
    }
    registry.add(filtered_replicator);
  }
  return 0;
}

static int deinit(plugin::Registry &registry)
{
  if (filtered_replicator)
  {
    registry.remove(filtered_replicator);
    delete filtered_replicator;
  }
  return 0;
}

static int check_filtered_schemas(Session *, 
                                  struct st_mysql_sys_var *,
                                  void *,
                                  struct st_mysql_value *value)
{
  char buff[STRING_BUFFER_USUAL_SIZE];
  int len= sizeof(buff);
  const char *input= value->val_str(value, buff, &len);

  if (input && filtered_replicator)
  {
    filtered_replicator->setSchemaFilter(input);
    return 0;
  }
  return 1;
}

static void set_filtered_schemas(Session *,
                                 struct st_mysql_sys_var *,
                                 void *var_ptr,
                                 const void *save)
{
  if (filtered_replicator)
  {
    if (*(bool *)save != true)
    {
      /* update the value of the system variable */
      filtered_replicator->updateSchemaSysvar((const char **) var_ptr);
    }
  }
}

static int check_filtered_tables(Session *, 
                                 struct st_mysql_sys_var *,
                                 void *,
                                 struct st_mysql_value *value)
{
  char buff[STRING_BUFFER_USUAL_SIZE];
  int len= sizeof(buff);
  const char *input= value->val_str(value, buff, &len);

  if (input && filtered_replicator)
  {
    filtered_replicator->setTableFilter(input);
    return 0;
  }
  return 1;
}

static void set_filtered_tables(Session *,
                                struct st_mysql_sys_var *,
                                void *var_ptr,
                                const void *save)
{
  if (filtered_replicator)
  {
    if (*(bool *)save != true)
    {
      /* update the value of the system variable */
      filtered_replicator->updateTableSysvar((const char **) var_ptr);
    }
  }
}

static DRIZZLE_SYSVAR_BOOL(enable,
                           sysvar_filtered_replicator_enabled,
                           PLUGIN_VAR_NOCMDARG,
                           N_("Enable filtered replicator"),
                           NULL, /* check func */
                           NULL, /* update func */
                           false /*default */);
static DRIZZLE_SYSVAR_STR(filteredschemas,
                          sysvar_filtered_replicator_sch_filters,
                          PLUGIN_VAR_OPCMDARG,
                          N_("List of schemas to filter"),
                          check_filtered_schemas,
                          set_filtered_schemas,
                          "");
static DRIZZLE_SYSVAR_STR(filteredtables,
                          sysvar_filtered_replicator_tab_filters,
                          PLUGIN_VAR_OPCMDARG,
                          N_("List of tables to filter"),
                          check_filtered_tables,
                          set_filtered_tables,
                          "");
static DRIZZLE_SYSVAR_STR(schemaregex,
                          sysvar_filtered_replicator_sch_regex,
                          PLUGIN_VAR_READONLY,
                          N_("Regular expression to apply to schemas to filter"),
                          NULL,
                          NULL,
                          NULL);
static DRIZZLE_SYSVAR_STR(tableregex,
                          sysvar_filtered_replicator_tab_regex,
                          PLUGIN_VAR_READONLY,
                          N_("Regular expression to apply to tables to filter"),
                          NULL,
                          NULL,
                          NULL);

static struct st_mysql_sys_var* filtered_replicator_system_variables[]= {
  DRIZZLE_SYSVAR(enable),
  DRIZZLE_SYSVAR(filteredschemas),
  DRIZZLE_SYSVAR(filteredtables),
  DRIZZLE_SYSVAR(schemaregex),
  DRIZZLE_SYSVAR(tableregex),
  NULL
};

drizzle_declare_plugin(filtered_replicator)
{
  "filtered_replicator",
  "0.2",
  "Padraig O'Sullivan",
  N_("Filtered Replicator"),
  PLUGIN_LICENSE_GPL,
  init, /* Plugin Init */
  deinit, /* Plugin Deinit */
  NULL, /* status variables */
  filtered_replicator_system_variables, /* system variables */
  NULL    /* config options */
}
drizzle_declare_plugin_end;
