/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 MySQL
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
  Drizzle Slap

  A simple program designed to work as if multiple clients querying the database,
  then reporting the timing of each stage.

  Drizzle slap runs three stages:
  1) Create schema,table, and optionally any SP or data you want to beign
  the test with. (single client)
  2) Load test (many clients)
  3) Cleanup (disconnection, drop table if specified, single client)

  Examples:

  Supply your own create and query SQL statements, with 50 clients
  querying (200 selects for each):

  drizzleslap --delimiter=";" \
  --create="CREATE TABLE A (a int);INSERT INTO A VALUES (23)" \
  --query="SELECT * FROM A" --concurrency=50 --iterations=200

  Let the program build the query SQL statement with a table of two int
  columns, three varchar columns, five clients querying (20 times each),
  don't create the table or insert the data (using the previous test's
  schema and data):

  drizzleslap --concurrency=5 --iterations=20 \
  --number-int-cols=2 --number-char-cols=3 \
  --auto-generate-sql

  Tell the program to load the create, insert and query SQL statements from
  the specified files, where the create.sql file has multiple table creation
  statements delimited by ';' and multiple insert statements delimited by ';'.
  The --query file will have multiple queries delimited by ';', run all the
  load statements, and then run all the queries in the query file
  with five clients (five times each):

  drizzleslap --concurrency=5 \
  --iterations=5 --query=query.sql --create=create.sql \
  --delimiter=";"

  @todo
  Add language for better tests
  String length for files and those put on the command line are not
  setup to handle binary data.
  More stats
  Break up tests and run them on multiple hosts at once.
  Allow output to be fed into a database directly.

*/

#include "config.h"

#include "client_priv.h"
#include <signal.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#include <fcntl.h>
#include <math.h>
#include <cassert>
#include <cstdlib>
#include <string>
#include <iostream>
#include <fstream>
#include <pthread.h>
#include <drizzled/configmake.h>
/* Added this for string translation. */
#include <drizzled/gettext.h>
#include <boost/program_options.hpp>

#define SLAP_VERSION "1.5"

#define HUGE_STRING_LENGTH 8196
#define RAND_STRING_SIZE 126
#define DEFAULT_BLOB_SIZE 1024

using namespace std;
using namespace drizzled;
namespace po= boost::program_options;

#ifdef HAVE_SMEM
static char *shared_memory_base_name=0;
#endif

/* Global Thread counter */
uint32_t thread_counter;
pthread_mutex_t counter_mutex;
pthread_cond_t count_threshhold;
uint32_t master_wakeup;
pthread_mutex_t sleeper_mutex;
pthread_cond_t sleep_threshhold;

/* Global Thread timer */
static bool timer_alarm= false;
pthread_mutex_t timer_alarm_mutex;
pthread_cond_t timer_alarm_threshold;

char **primary_keys;
/* This gets passed to malloc, so lets set it to an arch-dependant size */
size_t primary_keys_number_of;

static string host, 
  opt_password, 
  user,
  user_supplied_query,
  user_supplied_pre_statements,
  user_supplied_post_statements,
  default_engine,
  pre_system,
  post_system;

static vector<string> user_supplied_queries;
static string opt_verbose;
string delimiter;

string create_schema_string;

static bool opt_mysql;
static bool opt_preserve= true;
static bool opt_only_print;
static bool opt_burnin;
static bool opt_ignore_sql_errors= false;
static bool opt_silent,
  auto_generate_sql_autoincrement,
  auto_generate_sql_guid_primary,
  auto_generate_sql;
std::string opt_auto_generate_sql_type;

static int32_t verbose= 0;
static uint32_t delimiter_length;
static uint32_t commit_rate;
static uint32_t detach_rate;
static uint32_t opt_timer_length;
static uint32_t opt_delayed_start;
string num_blob_cols_opt,
  num_char_cols_opt,
  num_int_cols_opt;
string opt_label;
static unsigned int opt_set_random_seed;

string auto_generate_selected_columns_opt;

/* Yes, we do set defaults here */
static unsigned int num_int_cols= 1;
static unsigned int num_char_cols= 1;
static unsigned int num_blob_cols= 0;
static unsigned int num_blob_cols_size;
static unsigned int num_blob_cols_size_min;
static unsigned int num_int_cols_index= 0;
static unsigned int num_char_cols_index= 0;
static uint32_t iterations;
static uint64_t actual_queries= 0;
static uint64_t auto_actual_queries;
static uint64_t auto_generate_sql_unique_write_number;
static uint64_t auto_generate_sql_unique_query_number;
static uint32_t auto_generate_sql_secondary_indexes;
static uint64_t num_of_query;
static uint64_t auto_generate_sql_number;
string concurrency_str;
string create_string;
uint32_t *concurrency;

const char *default_dbug_option= "d:t:o,/tmp/drizzleslap.trace";
std::string opt_csv_str;
int csv_file;

static int process_options(void);
static uint32_t opt_drizzle_port= 0;


/* Types */
typedef enum {
  SELECT_TYPE= 0,
  UPDATE_TYPE= 1,
  INSERT_TYPE= 2,
  UPDATE_TYPE_REQUIRES_PREFIX= 3,
  CREATE_TABLE_TYPE= 4,
  SELECT_TYPE_REQUIRES_PREFIX= 5,
  DELETE_TYPE_REQUIRES_PREFIX= 6
} slap_query_type;

class Statement;

class Statement 
{
public:
  Statement(char *in_string,
            size_t in_length,
            slap_query_type in_type,
            char *in_option,
            size_t in_option_length,
            Statement *in_next) :
    string(in_string),
    length(in_length),
    type(in_type),
    option(in_option),
    option_length(in_option_length),
    next(in_next)
  { }

  Statement() :
    string(NULL),
    length(0),
    type(),
    option(NULL),
    option_length(0),
    next(NULL)
  { }

  ~Statement()
  {
    if (string)
      free(string);
  }
   
  char *getString() const
  {
    return string;
  }

  size_t getLength() const
  {
    return length;
  }

  slap_query_type getType() const
  {
    return type;
  }

  char *getOption() const
  {
    return option;
  }

  size_t getOptionLength() const
  {
    return option_length;
  }

  Statement *getNext() const
  {
    return next;
  }

  void setString(char *in_string)
  {
    string= in_string;
  }

  void setString(size_t in_length, char in_char)
  {
    string[in_length]= in_char;
  }

  void setLength(size_t in_length)
  {
    length= in_length;
  }

  void setType(slap_query_type in_type)
  {
    type= in_type;
  }

  void setOption(char *in_option)
  {
    option= in_option;
  }

   void setOptionLength(size_t in_option_length)
  {
    option_length= in_option_length;
  }

  void setNext(Statement *in_next)
  {
    next= in_next;
  }

private:
  char *string;
  size_t length;
  slap_query_type type;
  char *option;
  size_t option_length;
  Statement *next;
};

class OptionString;

class OptionString 
{
public:
  OptionString(char *in_string,
               size_t in_length,
               char *in_option,
               size_t in_option_length,
               OptionString *in_next) :
    string(in_string),
    length(in_length),
    option(in_option),
    option_length(in_option_length),
    next(in_next)
  { }  

  OptionString() :
    string(NULL),
    length(0),
    option(NULL),
    option_length(0),
    next(NULL)
  { }

  char *getString() const
  {
    return string;
  }

  size_t getLength() const
  {
    return length;
  }

  char *getOption() const
  {
  return option;
  }

  size_t getOptionLength() const
  {
    return option_length;
  }

  OptionString *getNext() const
  {
    return next;
  }

  void setString(char *in_string)
  {
    string= in_string;
  }

  void setOptionLength(size_t in_option_length)
  {
    option_length= in_option_length;
  }

  void setLength(size_t in_length)
  {
    length= in_length;
  }

  void setOption(char *in_option)
  {
    option= in_option;
  }

  void setOption(size_t in_option_length, char in_char)
  {
    option[in_option_length]= in_char;
  }

  void setNext(OptionString *in_next)
  {
    next= in_next;
  }
  
private:
  char *string;
  size_t length;
  char *option;
  size_t option_length;
  OptionString *next;
};

class Stats;

class Stats 
{
public:
  Stats(long int in_timing,
        uint32_t in_users,
        uint32_t in_real_users,
        uint32_t in_rows,
        long int in_create_timing,
        uint64_t in_create_count)
    :
    timing(in_timing),
    users(in_users),
    real_users(in_real_users),
    rows(in_rows),
    create_timing(in_create_timing),
    create_count(in_create_count)
    {}

  Stats()
    :
    timing(0),
    users(0),
    real_users(0),
    rows(0),
    create_timing(0),
    create_count(0)
    {}

  long int getTiming() const
  {
    return timing;
  }

  uint32_t getUsers() const
  {
    return users;
  }   

  uint32_t getRealUsers() const
  {
    return real_users;
  }

  uint64_t getRows() const
  {
    return rows;
  }

  long int getCreateTiming() const
  {
    return create_timing;
  }

  uint64_t getCreateCount() const
  {
    return create_count;
  }

  void setTiming(long int in_timing)
  {
  timing= in_timing;
  }

  void setUsers(uint32_t in_users)
  {
    users= in_users;
  }

  void setRealUsers(uint32_t in_real_users)
  {
    real_users= in_real_users;
  }

  void setRows(uint64_t in_rows)
  {
    rows= in_rows;
  }
   
  void setCreateTiming(long int in_create_timing)
  {
    create_timing= in_create_timing;
  }

  void setCreateCount(uint64_t in_create_count)
  {
  create_count= in_create_count;
  }
  
private:
  long int timing;
  uint32_t users;
  uint32_t real_users;
  uint64_t rows;
  long int create_timing;
  uint64_t create_count;
};

class ThreadContext;

class ThreadContext 
{
public:
  ThreadContext(Statement *in_stmt,
                uint64_t in_limit)
    :
    stmt(in_stmt),
    limit(in_limit)
    {}

  ThreadContext()
    :
    stmt(),
    limit(0)
    {}

  Statement *getStmt() const
  {
    return stmt;
  }

  uint64_t getLimit() const
  {
    return limit;
  }

  void setStmt(Statement *in_stmt)
  {
    stmt= in_stmt;
  }

  void setLimit(uint64_t in_limit)
  {
    limit= in_limit;
  }  

private:
  Statement *stmt;
  uint64_t limit;
};

class Conclusions;

class Conclusions 
{

public:
  Conclusions(char *in_engine,
              long int in_avg_timing,
              long int in_max_timing,
              long int in_min_timing,
              uint32_t in_users,
              uint32_t in_real_users,
              uint64_t in_avg_rows,
              long int in_sum_of_time,
              long int in_std_dev,
              long int in_create_avg_timing,
              long int in_create_max_timing,
              long int in_create_min_timing,
              uint64_t in_create_count,
              uint64_t in_max_rows,
              uint64_t in_min_rows)
    :
    engine(in_engine),
    avg_timing(in_avg_timing),
    max_timing(in_max_timing),
    min_timing(in_min_timing),
    users(in_users),
    real_users(in_real_users),
    avg_rows(in_avg_rows),
    sum_of_time(in_sum_of_time),
    std_dev(in_std_dev),
    create_avg_timing(in_create_avg_timing),
    create_max_timing(in_create_max_timing),
    create_min_timing(in_create_min_timing),
    create_count(in_create_count),
    max_rows(in_max_rows),
    min_rows(in_min_rows)
    {}

  Conclusions()
    :
    engine(NULL),
    avg_timing(0),
    max_timing(0),
    min_timing(0),
    users(0),
    real_users(0),
    avg_rows(0),
    sum_of_time(0),
    std_dev(0),
    create_avg_timing(0),
    create_max_timing(0),
    create_min_timing(0),
    create_count(0),
    max_rows(0),
    min_rows(0)
    {}

  char *getEngine() const
  {
    return engine;
  }
  
  long int getAvgTiming() const
  {
    return avg_timing;
  }

  long int getMaxTiming() const
  {
    return max_timing;
  }

  long int getMinTiming() const
  {
    return min_timing;
  }

  uint32_t getUsers() const
  {
    return users;
  }

  uint32_t getRealUsers() const
  {
    return real_users;
  }

  uint64_t getAvgRows() const
  {
    return avg_rows;
  }   

  long int getSumOfTime() const
  {
    return sum_of_time;
  }

  long int getStdDev() const
  {
    return std_dev;
  }

  long int getCreateAvgTiming() const
  {
    return create_avg_timing;
  }

  long int getCreateMaxTiming() const
  {
    return create_max_timing;
  }

  long int getCreateMinTiming() const
  {
    return create_min_timing;
  }
   
  uint64_t getCreateCount() const
  {
    return create_count;
  }

  uint64_t getMinRows() const
  {
    return min_rows;
  }

  uint64_t getMaxRows() const
  {
    return max_rows;
  }

  void setEngine(char *in_engine) 
  {
    engine= in_engine;
  }
  
  void setAvgTiming(long int in_avg_timing) 
  {
    avg_timing= in_avg_timing;
  }

  void setMaxTiming(long int in_max_timing) 
  {
    max_timing= in_max_timing;
  }

  void setMinTiming(long int in_min_timing) 
  {
    min_timing= in_min_timing;
  }

  void setUsers(uint32_t in_users) 
  {
    users= in_users;
  }

  void setRealUsers(uint32_t in_real_users) 
  {
    real_users= in_real_users;
  }

  void setAvgRows(uint64_t in_avg_rows) 
  {
    avg_rows= in_avg_rows;
  }   

  void setSumOfTime(long int in_sum_of_time) 
  {
    sum_of_time= in_sum_of_time;
  }

  void setStdDev(long int in_std_dev) 
  {
    std_dev= in_std_dev;
  }

  void setCreateAvgTiming(long int in_create_avg_timing) 
  {
    create_avg_timing= in_create_avg_timing;
  }

  void setCreateMaxTiming(long int in_create_max_timing) 
  {
    create_max_timing= in_create_max_timing;
  }

  void setCreateMinTiming(long int in_create_min_timing) 
  {
    create_min_timing= in_create_min_timing;
  }
   
  void setCreateCount(uint64_t in_create_count) 
  {
    create_count= in_create_count;
  }

  void setMinRows(uint64_t in_min_rows) 
  {
    min_rows= in_min_rows;
  }

  void setMaxRows(uint64_t in_max_rows) 
  {
    max_rows= in_max_rows;
  }

private:
  char *engine;
  long int avg_timing;
  long int max_timing;
  long int min_timing;
  uint32_t users;
  uint32_t real_users;
  uint64_t avg_rows;
  long int sum_of_time;
  long int std_dev;
  /* These are just for create time stats */
  long int create_avg_timing;
  long int create_max_timing;
  long int create_min_timing;
  uint64_t create_count;
  /* The following are not used yet */
  uint64_t max_rows;
  uint64_t min_rows;
};


static OptionString *engine_options= NULL;
static OptionString *query_options= NULL;
static Statement *pre_statements= NULL;
static Statement *post_statements= NULL;
static Statement *create_statements= NULL;

static Statement **query_statements= NULL;
static unsigned int query_statements_count;


/* Prototypes */
void print_conclusions(Conclusions *con);
void print_conclusions_csv(Conclusions *con);
void generate_stats(Conclusions *con, OptionString *eng, Stats *sptr);
uint32_t parse_comma(const char *string, uint32_t **range);
uint32_t parse_delimiter(const char *script, Statement **stmt, char delm);
uint32_t parse_option(const char *origin, OptionString **stmt, char delm);
static int drop_schema(drizzle_con_st *con, const char *db);
uint32_t get_random_string(char *buf, size_t size);
static Statement *build_table_string(void);
static Statement *build_insert_string(void);
static Statement *build_update_string(void);
static Statement * build_select_string(bool key);
static int generate_primary_key_list(drizzle_con_st *con, OptionString *engine_stmt);
static int drop_primary_key_list(void);
static int create_schema(drizzle_con_st *con, const char *db, Statement *stmt,
                         OptionString *engine_stmt, Stats *sptr);
static int run_scheduler(Stats *sptr, Statement **stmts, uint32_t concur,
                         uint64_t limit);
extern "C" pthread_handler_t run_task(void *p);
extern "C" pthread_handler_t timer_thread(void *p);
void statement_cleanup(Statement *stmt);
void option_cleanup(OptionString *stmt);
void concurrency_loop(drizzle_con_st *con, uint32_t current, OptionString *eptr);
static int run_statements(drizzle_con_st *con, Statement *stmt);
void slap_connect(drizzle_con_st *con, bool connect_to_schema);
void slap_close(drizzle_con_st *con);
static int run_query(drizzle_con_st *con, drizzle_result_st *result, const char *query, int len);
void standard_deviation (Conclusions *con, Stats *sptr);

static const char ALPHANUMERICS[]=
"0123456789ABCDEFGHIJKLMNOPQRSTWXYZabcdefghijklmnopqrstuvwxyz";

#define ALPHANUMERICS_SIZE (sizeof(ALPHANUMERICS)-1)


static long int timedif(struct timeval a, struct timeval b)
{
  register int us, s;

  us = a.tv_usec - b.tv_usec;
  us /= 1000;
  s = a.tv_sec - b.tv_sec;
  s *= 1000;
  return s + us;
}

static void combine_queries(vector<string> queries)
{
  user_supplied_query.erase();
  for (vector<string>::iterator it= queries.begin();
       it != queries.end();
       ++it)
  {
    user_supplied_query.append(*it);
    user_supplied_query.append(delimiter);
  }
}
/**
 * commandline_options is the set of all options that can only be called via the command line.

 * client_options is the set of all options that can be defined via both command line and via
 * the configuration file client.cnf

 * slap_options is the set of all drizzleslap specific options which behave in a manner 
 * similar to that of client_options. It's configuration file is drizzleslap.cnf

 * long_options is the union of commandline_options, slap_options and client_options.

 * There are two configuration files per set of options, one which is defined by the user
 * which is found at either $XDG_CONFIG_HOME/drizzle or ~/.config/drizzle directory and the other which 
 * is the system configuration file which is found in the SYSCONFDIR/drizzle directory.

 * The system configuration file is over ridden by the user's configuration file which
 * in turn is over ridden by the command line.
 */
int main(int argc, char **argv)
{
  char *password= NULL;
  try
  {
    po::options_description commandline_options("Options used only in command line");
    commandline_options.add_options()
      ("help,?","Display this help and exit")
      ("info,i","Gives information and exit")
      ("burnin",po::value<bool>(&opt_burnin)->default_value(false)->zero_tokens(),
       "Run full test case in infinite loop")
      ("ignore-sql-errors", po::value<bool>(&opt_ignore_sql_errors)->default_value(false)->zero_tokens(),
       "Ignore SQL errors in query run")
      ("create-schema",po::value<string>(&create_schema_string)->default_value("drizzleslap"),
       "Schema to run tests in")
      ("create",po::value<string>(&create_string)->default_value(""),
       "File or string to use to create tables")
      ("detach",po::value<uint32_t>(&detach_rate)->default_value(0),
       "Detach (close and re open) connections after X number of requests")
      ("iterations,i",po::value<uint32_t>(&iterations)->default_value(1),
       "Number of times to run the tests")
      ("label",po::value<string>(&opt_label)->default_value(""),
       "Label to use for print and csv")
      ("number-blob-cols",po::value<string>(&num_blob_cols_opt)->default_value(""),
       "Number of BLOB columns to create table with if specifying --auto-generate-sql. Example --number-blob-cols=3:1024/2048 would give you 3 blobs with a random size between 1024 and 2048. ")
      ("number-char-cols,x",po::value<string>(&num_char_cols_opt)->default_value(""),
       "Number of VARCHAR columns to create in table if specifying --auto-generate-sql.")
      ("number-int-cols,y",po::value<string>(&num_int_cols_opt)->default_value(""),
       "Number of INT columns to create in table if specifying --auto-generate-sql.")
      ("number-of-queries",
       po::value<uint64_t>(&num_of_query)->default_value(0),
       "Limit each client to this number of queries(this is not exact)") 
      ("only-print",po::value<bool>(&opt_only_print)->default_value(false)->zero_tokens(),
       "This causes drizzleslap to not connect to the database instead print out what it would have done instead")
      ("post-query", po::value<string>(&user_supplied_post_statements)->default_value(""),
       "Query to run or file containing query to execute after tests have completed.")
      ("post-system",po::value<string>(&post_system)->default_value(""),
       "system() string to execute after tests have completed")
      ("pre-query",
       po::value<string>(&user_supplied_pre_statements)->default_value(""),
       "Query to run or file containing query to execute before running tests.")
      ("pre-system",po::value<string>(&pre_system)->default_value(""),
       "system() string to execute before running tests.")
      ("query,q",po::value<vector<string> >(&user_supplied_queries)->composing()->notifier(&combine_queries),
       "Query to run or file containing query")
      ("verbose,v", po::value<string>(&opt_verbose)->default_value("v"), "Increase verbosity level by one.")
      ("version,V","Output version information and exit") 
      ;

    po::options_description slap_options("Options specific to drizzleslap");
    slap_options.add_options()
      ("auto-generate-sql-select-columns",
       po::value<string>(&auto_generate_selected_columns_opt)->default_value(""),
       "Provide a string to use for the select fields used in auto tests")
      ("auto-generate-sql,a",po::value<bool>(&auto_generate_sql)->default_value(false)->zero_tokens(),
       "Generate SQL where not supplied by file or command line")  
      ("auto-generate-sql-add-autoincrement",
       po::value<bool>(&auto_generate_sql_autoincrement)->default_value(false)->zero_tokens(),
       "Add an AUTO_INCREMENT column to auto-generated tables")
      ("auto-generate-sql-execute-number",
       po::value<uint64_t>(&auto_actual_queries)->default_value(0),
       "See this number and generate a set of queries to run")
      ("auto-generate-sql-guid-primary",
       po::value<bool>(&auto_generate_sql_guid_primary)->default_value(false)->zero_tokens(),
       "Add GUID based primary keys to auto-generated tables")
      ("auto-generate-sql-load-type",
       po::value<string>(&opt_auto_generate_sql_type)->default_value("mixed"),
       "Specify test load type: mixed, update, write, key or read; default is mixed")  
      ("auto-generate-sql-secondary-indexes",
       po::value<uint32_t>(&auto_generate_sql_secondary_indexes)->default_value(0),
       "Number of secondary indexes to add to auto-generated tables")
      ("auto-generated-sql-unique-query-number",
       po::value<uint64_t>(&auto_generate_sql_unique_query_number)->default_value(10),
       "Number of unique queries to generate for automatic tests")
      ("auto-generate-sql-unique-write-number",
       po::value<uint64_t>(&auto_generate_sql_unique_write_number)->default_value(10),
       "Number of unique queries to generate for auto-generate-sql-write-number")
      ("auto-generate-sql-write-number",
       po::value<uint64_t>(&auto_generate_sql_number)->default_value(100),
       "Number of row inserts to perform for each thread (default is 100).")
      ("commit",po::value<uint32_t>(&commit_rate)->default_value(0),
       "Commit records every X number of statements")
      ("concurrency,c",po::value<string>(&concurrency_str)->default_value(""),
       "Number of clients to simulate for query to run")
      ("csv",po::value<std::string>(&opt_csv_str)->default_value(""),
       "Generate CSV output to named file or to stdout if no file is name.")
      ("delayed-start",po::value<uint32_t>(&opt_delayed_start)->default_value(0),
       "Delay the startup of threads by a random number of microsends (the maximum of the delay")
      ("delimiter,F",po::value<string>(&delimiter)->default_value("\n"),
       "Delimiter to use in SQL statements supplied in file or command line")
      ("engine ,e",po::value<string>(&default_engine)->default_value(""),
       "Storage engien to use for creating the table")
      ("set-random-seed",
       po::value<uint32_t>(&opt_set_random_seed)->default_value(0), 
       "Seed for random number generator (srandom(3)) ") 
      ("silent,s",po::value<bool>(&opt_silent)->default_value(false)->zero_tokens(),
       "Run program in silent mode - no output. ") 
      ("timer-length",po::value<uint32_t>(&opt_timer_length)->default_value(0),
       "Require drizzleslap to run each specific test a certain amount of time in seconds")  
      ;

    po::options_description client_options("Options specific to the client");
    client_options.add_options()
      ("mysql,m", po::value<bool>(&opt_mysql)->default_value(true)->zero_tokens(),
       N_("Use MySQL Protocol."))
      ("host,h",po::value<string>(&host)->default_value("localhost"),"Connect to the host")
      ("password,P",po::value<char *>(&password),
       "Password to use when connecting to server. If password is not given it's asked from the tty")
      ("port,p",po::value<uint32_t>(), "Port number to use for connection")
      ("protocol",po::value<string>(),
       "The protocol of connection (tcp,socket,pipe,memory).")
      ("user,u",po::value<string>(&user)->default_value(""),
       "User for login if not current user")  
      ;

    po::options_description long_options("Allowed Options");
    long_options.add(commandline_options).add(slap_options).add(client_options);

    std::string system_config_dir_slap(SYSCONFDIR); 
    system_config_dir_slap.append("/drizzle/drizzleslap.cnf");

    std::string system_config_dir_client(SYSCONFDIR); 
    system_config_dir_client.append("/drizzle/client.cnf");

    std::string user_config_dir((getenv("XDG_CONFIG_HOME")? getenv("XDG_CONFIG_HOME"):"~/.config"));

    uint64_t temp_drizzle_port= 0;
    drizzle_con_st con;
    OptionString *eptr;
    uint32_t x;


    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(long_options).
            extra_parser(parse_password_arg).run(), vm);

    std::string user_config_dir_slap(user_config_dir);
    user_config_dir_slap.append("/drizzle/drizzleslap.cnf"); 

    std::string user_config_dir_client(user_config_dir);
    user_config_dir_client.append("/drizzle/client.cnf");

    ifstream user_slap_ifs(user_config_dir_slap.c_str());
    po::store(parse_config_file(user_slap_ifs, slap_options), vm);

    ifstream user_client_ifs(user_config_dir_client.c_str());
    po::store(parse_config_file(user_client_ifs, client_options), vm);

    ifstream system_slap_ifs(system_config_dir_slap.c_str());
    store(parse_config_file(system_slap_ifs, slap_options), vm);

    ifstream system_client_ifs(system_config_dir_client.c_str());
    store(parse_config_file(system_client_ifs, client_options), vm);

    po::notify(vm);

    if (process_options())
      exit(1);

    if ( vm.count("help") || vm.count("info"))
    {
      printf("%s  Ver %s Distrib %s, for %s-%s (%s)\n",internal::my_progname, SLAP_VERSION,
          drizzle_version(),HOST_VENDOR,HOST_OS,HOST_CPU);
      puts("Copyright (C) 2008 Sun Microsystems");
      puts("This software comes with ABSOLUTELY NO WARRANTY. This is free software,\
          \nand you are welcome to modify and redistribute it under the GPL \
          license\n");
      puts("Run a query multiple times against the server\n");
      cout << long_options << endl;
      exit(0);
    }   

    if (vm.count("port")) 
    {
      temp_drizzle_port= vm["port"].as<uint32_t>();

      if ((temp_drizzle_port == 0) || (temp_drizzle_port > 65535))
      {
        fprintf(stderr, _("Value supplied for port is not valid.\n"));
        exit(1);
      }
      else
      {
        opt_drizzle_port= (uint32_t) temp_drizzle_port;
      }
    }

  if ( vm.count("password") )
  {
    if (!opt_password.empty())
      opt_password.erase();
    if (password == PASSWORD_SENTINEL)
    {
      opt_password= "";
    }
    else
    {
      opt_password= password;
      tty_password= false;
    }
  }
  else
  {
      tty_password= true;
  }



    if ( vm.count("version") )
    {
      printf("%s  Ver %s Distrib %s, for %s-%s (%s)\n",internal::my_progname, SLAP_VERSION,
          drizzle_version(),HOST_VENDOR,HOST_OS,HOST_CPU);
      exit(0);
    }

    /* Seed the random number generator if we will be using it. */
    if (auto_generate_sql)
    {
      if (opt_set_random_seed == 0)
        opt_set_random_seed= (unsigned int)time(NULL);
      srandom(opt_set_random_seed);
    }

    /* globals? Yes, so we only have to run strlen once */
    delimiter_length= delimiter.length();

    slap_connect(&con, false);

    pthread_mutex_init(&counter_mutex, NULL);
    pthread_cond_init(&count_threshhold, NULL);
    pthread_mutex_init(&sleeper_mutex, NULL);
    pthread_cond_init(&sleep_threshhold, NULL);
    pthread_mutex_init(&timer_alarm_mutex, NULL);
    pthread_cond_init(&timer_alarm_threshold, NULL);


    /* Main iterations loop */
burnin:
    eptr= engine_options;
    do
    {
      /* For the final stage we run whatever queries we were asked to run */
      uint32_t *current;

      if (verbose >= 2)
        printf("Starting Concurrency Test\n");

      if (*concurrency)
      {
        for (current= concurrency; current && *current; current++)
          concurrency_loop(&con, *current, eptr);
      }
      else
      {
        uint32_t infinite= 1;
        do {
          concurrency_loop(&con, infinite, eptr);
        }
        while (infinite++);
      }

      if (!opt_preserve)
        drop_schema(&con, create_schema_string.c_str());

    } while (eptr ? (eptr= eptr->getNext()) : 0);

    if (opt_burnin)
      goto burnin;

    pthread_mutex_destroy(&counter_mutex);
    pthread_cond_destroy(&count_threshhold);
    pthread_mutex_destroy(&sleeper_mutex);
    pthread_cond_destroy(&sleep_threshhold);
    pthread_mutex_destroy(&timer_alarm_mutex);
    pthread_cond_destroy(&timer_alarm_threshold);

    slap_close(&con);

    /* now free all the strings we created */
    if (!opt_password.empty())
      opt_password.erase();

    free(concurrency);

    statement_cleanup(create_statements);
    for (x= 0; x < query_statements_count; x++)
      statement_cleanup(query_statements[x]);
    free(query_statements);
    statement_cleanup(pre_statements);
    statement_cleanup(post_statements);
    option_cleanup(engine_options);
    option_cleanup(query_options);

#ifdef HAVE_SMEM
    if (shared_memory_base_name)
      free(shared_memory_base_name);
#endif

  }

  catch(std::exception &err)
  {
    cerr<<"Error:"<<err.what()<<endl;
  }

  if (csv_file != fileno(stdout))
    close(csv_file);

  return 0;
}

void concurrency_loop(drizzle_con_st *con, uint32_t current, OptionString *eptr)
{
  unsigned int x;
  Stats *head_sptr;
  Stats *sptr;
  Conclusions conclusion;
  uint64_t client_limit;

  head_sptr= new Stats[iterations];
  if (head_sptr == NULL)
  {
    fprintf(stderr,"Error allocating memory in concurrency_loop\n");
    exit(1);
  }

  if (auto_actual_queries)
    client_limit= auto_actual_queries;
  else if (num_of_query)
    client_limit=  num_of_query / current;
  else
    client_limit= actual_queries;

  for (x= 0, sptr= head_sptr; x < iterations; x++, sptr++)
  {
    /*
      We might not want to load any data, such as when we are calling
      a stored_procedure that doesn't use data, or we know we already have
      data in the table.
    */
    if (opt_preserve == false)
      drop_schema(con, create_schema_string.c_str());

    /* First we create */
    if (create_statements)
      create_schema(con, create_schema_string.c_str(), create_statements, eptr, sptr);

    /*
      If we generated GUID we need to build a list of them from creation that
      we can later use.
    */
    if (verbose >= 2)
      printf("Generating primary key list\n");
    if (auto_generate_sql_autoincrement || auto_generate_sql_guid_primary)
      generate_primary_key_list(con, eptr);

    if (commit_rate)
      run_query(con, NULL, "SET AUTOCOMMIT=0", strlen("SET AUTOCOMMIT=0"));

    if (!pre_system.empty())
    {
      int ret= system(pre_system.c_str());
      assert(ret != -1);
    }
       

    /*
      Pre statements are always run after all other logic so they can
      correct/adjust any item that they want.
    */
    if (pre_statements)
      run_statements(con, pre_statements);

    run_scheduler(sptr, query_statements, current, client_limit);

    if (post_statements)
      run_statements(con, post_statements);

    if (!post_system.empty())
    {
      int ret=  system(post_system.c_str());
      assert(ret !=-1);
    }

    /* We are finished with this run */
    if (auto_generate_sql_autoincrement || auto_generate_sql_guid_primary)
      drop_primary_key_list();
  }

  if (verbose >= 2)
    printf("Generating stats\n");

  generate_stats(&conclusion, eptr, head_sptr);

  if (!opt_silent)
    print_conclusions(&conclusion);
  if (!opt_csv_str.empty())
    print_conclusions_csv(&conclusion);

  free(head_sptr);

}



uint
get_random_string(char *buf, size_t size)
{
  char *buf_ptr= buf;
  size_t x;

  for (x= size; x > 0; x--)
    *buf_ptr++= ALPHANUMERICS[random() % ALPHANUMERICS_SIZE];
  return(buf_ptr - buf);
}


/*
  build_table_string

  This function builds a create table query if the user opts to not supply
  a file or string containing a create table statement
*/
static Statement *
build_table_string(void)
{
  char       buf[HUGE_STRING_LENGTH];
  unsigned int        col_count;
  Statement *ptr;
  string table_string;

  table_string.reserve(HUGE_STRING_LENGTH);

  table_string= "CREATE TABLE `t1` (";

  if (auto_generate_sql_autoincrement)
  {
    table_string.append("id serial");

    if (num_int_cols || num_char_cols)
      table_string.append(",");
  }

  if (auto_generate_sql_guid_primary)
  {
    table_string.append("id varchar(128) primary key");

    if (num_int_cols || num_char_cols || auto_generate_sql_guid_primary)
      table_string.append(",");
  }

  if (auto_generate_sql_secondary_indexes)
  {
    unsigned int count;

    for (count= 0; count < auto_generate_sql_secondary_indexes; count++)
    {
      if (count) /* Except for the first pass we add a comma */
        table_string.append(",");

      if (snprintf(buf, HUGE_STRING_LENGTH, "id%d varchar(32) unique key", count)
          > HUGE_STRING_LENGTH)
      {
        fprintf(stderr, "Memory Allocation error in create table\n");
        exit(1);
      }
      table_string.append(buf);
    }

    if (num_int_cols || num_char_cols)
      table_string.append(",");
  }

  if (num_int_cols)
    for (col_count= 1; col_count <= num_int_cols; col_count++)
    {
      if (num_int_cols_index)
      {
        if (snprintf(buf, HUGE_STRING_LENGTH, "intcol%d INT, INDEX(intcol%d)",
                     col_count, col_count) > HUGE_STRING_LENGTH)
        {
          fprintf(stderr, "Memory Allocation error in create table\n");
          exit(1);
        }
      }
      else
      {
        if (snprintf(buf, HUGE_STRING_LENGTH, "intcol%d INT ", col_count)
            > HUGE_STRING_LENGTH)
        {
          fprintf(stderr, "Memory Allocation error in create table\n");
          exit(1);
        }
      }
      table_string.append(buf);

      if (col_count < num_int_cols || num_char_cols > 0)
        table_string.append(",");
    }

  if (num_char_cols)
    for (col_count= 1; col_count <= num_char_cols; col_count++)
    {
      if (num_char_cols_index)
      {
        if (snprintf(buf, HUGE_STRING_LENGTH,
                     "charcol%d VARCHAR(128), INDEX(charcol%d) ",
                     col_count, col_count) > HUGE_STRING_LENGTH)
        {
          fprintf(stderr, "Memory Allocation error in creating table\n");
          exit(1);
        }
      }
      else
      {
        if (snprintf(buf, HUGE_STRING_LENGTH, "charcol%d VARCHAR(128)",
                     col_count) > HUGE_STRING_LENGTH)
        {
          fprintf(stderr, "Memory Allocation error in creating table\n");
          exit(1);
        }
      }
      table_string.append(buf);

      if (col_count < num_char_cols || num_blob_cols > 0)
        table_string.append(",");
    }

  if (num_blob_cols)
    for (col_count= 1; col_count <= num_blob_cols; col_count++)
    {
      if (snprintf(buf, HUGE_STRING_LENGTH, "blobcol%d blob",
                   col_count) > HUGE_STRING_LENGTH)
      {
        fprintf(stderr, "Memory Allocation error in creating table\n");
        exit(1);
      }
      table_string.append(buf);

      if (col_count < num_blob_cols)
        table_string.append(",");
    }

  table_string.append(")");
  ptr= new Statement;
  ptr->setString((char *)malloc(table_string.length()+1));
  if (ptr->getString()==NULL)
  {
    fprintf(stderr, "Memory Allocation error in creating table\n");
    exit(1);
  }
  memset(ptr->getString(), 0, table_string.length()+1);
  ptr->setLength(table_string.length()+1);
  ptr->setType(CREATE_TABLE_TYPE);
  strcpy(ptr->getString(), table_string.c_str());
  return(ptr);
}

/*
  build_update_string()

  This function builds insert statements when the user opts to not supply
  an insert file or string containing insert data
*/
static Statement *
build_update_string(void)
{
  char       buf[HUGE_STRING_LENGTH];
  unsigned int        col_count;
  Statement *ptr;
  string update_string;

  update_string.reserve(HUGE_STRING_LENGTH);

  update_string= "UPDATE t1 SET ";

  if (num_int_cols)
    for (col_count= 1; col_count <= num_int_cols; col_count++)
    {
      if (snprintf(buf, HUGE_STRING_LENGTH, "intcol%d = %ld", col_count,
                   random()) > HUGE_STRING_LENGTH)
      {
        fprintf(stderr, "Memory Allocation error in creating update\n");
        exit(1);
      }
      update_string.append(buf);

      if (col_count < num_int_cols || num_char_cols > 0)
        update_string.append(",", 1);
    }

  if (num_char_cols)
    for (col_count= 1; col_count <= num_char_cols; col_count++)
    {
      char rand_buffer[RAND_STRING_SIZE];
      int buf_len= get_random_string(rand_buffer, RAND_STRING_SIZE);

      if (snprintf(buf, HUGE_STRING_LENGTH, "charcol%d = '%.*s'", col_count,
                   buf_len, rand_buffer)
          > HUGE_STRING_LENGTH)
      {
        fprintf(stderr, "Memory Allocation error in creating update\n");
        exit(1);
      }
      update_string.append(buf);

      if (col_count < num_char_cols)
        update_string.append(",", 1);
    }

  if (auto_generate_sql_autoincrement || auto_generate_sql_guid_primary)
    update_string.append(" WHERE id = ");


  ptr= new Statement;

  ptr->setLength(update_string.length()+1);
  ptr->setString((char *)malloc(ptr->getLength()));
  if (ptr->getString() == NULL)
  {
    fprintf(stderr, "Memory Allocation error in creating update\n");
    exit(1);
  }
  memset(ptr->getString(), 0, ptr->getLength());
  if (auto_generate_sql_autoincrement || auto_generate_sql_guid_primary)
    ptr->setType(UPDATE_TYPE_REQUIRES_PREFIX);
  else
    ptr->setType(UPDATE_TYPE);
  strncpy(ptr->getString(), update_string.c_str(), ptr->getLength());
  return(ptr);
}


/*
  build_insert_string()

  This function builds insert statements when the user opts to not supply
  an insert file or string containing insert data
*/
static Statement *
build_insert_string(void)
{
  char       buf[HUGE_STRING_LENGTH];
  unsigned int        col_count;
  Statement *ptr;
  string insert_string;

  insert_string.reserve(HUGE_STRING_LENGTH);

  insert_string= "INSERT INTO t1 VALUES (";

  if (auto_generate_sql_autoincrement)
  {
    insert_string.append("NULL");

    if (num_int_cols || num_char_cols)
      insert_string.append(",");
  }

  if (auto_generate_sql_guid_primary)
  {
    insert_string.append("uuid()");

    if (num_int_cols || num_char_cols)
      insert_string.append(",");
  }

  if (auto_generate_sql_secondary_indexes)
  {
    unsigned int count;

    for (count= 0; count < auto_generate_sql_secondary_indexes; count++)
    {
      if (count) /* Except for the first pass we add a comma */
        insert_string.append(",");

      insert_string.append("uuid()");
    }

    if (num_int_cols || num_char_cols)
      insert_string.append(",");
  }

  if (num_int_cols)
    for (col_count= 1; col_count <= num_int_cols; col_count++)
    {
      if (snprintf(buf, HUGE_STRING_LENGTH, "%ld", random()) > HUGE_STRING_LENGTH)
      {
        fprintf(stderr, "Memory Allocation error in creating insert\n");
        exit(1);
      }
      insert_string.append(buf);

      if (col_count < num_int_cols || num_char_cols > 0)
        insert_string.append(",");
    }

  if (num_char_cols)
    for (col_count= 1; col_count <= num_char_cols; col_count++)
    {
      int buf_len= get_random_string(buf, RAND_STRING_SIZE);
      insert_string.append("'", 1);
      insert_string.append(buf, buf_len);
      insert_string.append("'", 1);

      if (col_count < num_char_cols || num_blob_cols > 0)
        insert_string.append(",", 1);
    }

  if (num_blob_cols)
  {
    char *blob_ptr;

    if (num_blob_cols_size > HUGE_STRING_LENGTH)
    {
      blob_ptr= (char *)malloc(sizeof(char)*num_blob_cols_size);
      if (!blob_ptr)
      {
        fprintf(stderr, "Memory Allocation error in creating select\n");
        exit(1);
      }
      memset(blob_ptr, 0, sizeof(char)*num_blob_cols_size);
    }
    else
    {
      blob_ptr= buf;
    }

    for (col_count= 1; col_count <= num_blob_cols; col_count++)
    {
      unsigned int buf_len;
      unsigned int size;
      unsigned int difference= num_blob_cols_size - num_blob_cols_size_min;

      size= difference ? (num_blob_cols_size_min + (random() % difference)) :
        num_blob_cols_size;

      buf_len= get_random_string(blob_ptr, size);

      insert_string.append("'", 1);
      insert_string.append(blob_ptr, buf_len);
      insert_string.append("'", 1);

      if (col_count < num_blob_cols)
        insert_string.append(",", 1);
    }

    if (num_blob_cols_size > HUGE_STRING_LENGTH)
      free(blob_ptr);
  }

  insert_string.append(")", 1);

  ptr= new Statement;
  ptr->setLength(insert_string.length()+1);
  ptr->setString((char *)malloc(ptr->getLength()));
  if (ptr->getString()==NULL)
  {
    fprintf(stderr, "Memory Allocation error in creating select\n");
    exit(1);
  }
  memset(ptr->getString(), 0, ptr->getLength());
  ptr->setType(INSERT_TYPE);
  strcpy(ptr->getString(), insert_string.c_str());
  return(ptr);
}


/*
  build_select_string()

  This function builds a query if the user opts to not supply a query
  statement or file containing a query statement
*/
static Statement *
build_select_string(bool key)
{
  char       buf[HUGE_STRING_LENGTH];
  unsigned int        col_count;
  Statement *ptr;
  string query_string;

  query_string.reserve(HUGE_STRING_LENGTH);

  query_string.append("SELECT ", 7);
  if (!auto_generate_selected_columns_opt.empty())
  {
    query_string.append(auto_generate_selected_columns_opt.c_str());
  }
  else
  {
    for (col_count= 1; col_count <= num_int_cols; col_count++)
    {
      if (snprintf(buf, HUGE_STRING_LENGTH, "intcol%d", col_count)
          > HUGE_STRING_LENGTH)
      {
        fprintf(stderr, "Memory Allocation error in creating select\n");
        exit(1);
      }
      query_string.append(buf);

      if (col_count < num_int_cols || num_char_cols > 0)
        query_string.append(",", 1);

    }
    for (col_count= 1; col_count <= num_char_cols; col_count++)
    {
      if (snprintf(buf, HUGE_STRING_LENGTH, "charcol%d", col_count)
          > HUGE_STRING_LENGTH)
      {
        fprintf(stderr, "Memory Allocation error in creating select\n");
        exit(1);
      }
      query_string.append(buf);

      if (col_count < num_char_cols || num_blob_cols > 0)
        query_string.append(",", 1);

    }
    for (col_count= 1; col_count <= num_blob_cols; col_count++)
    {
      if (snprintf(buf, HUGE_STRING_LENGTH, "blobcol%d", col_count)
          > HUGE_STRING_LENGTH)
      {
        fprintf(stderr, "Memory Allocation error in creating select\n");
        exit(1);
      }
      query_string.append(buf);

      if (col_count < num_blob_cols)
        query_string.append(",", 1);
    }
  }
  query_string.append(" FROM t1");

  if ((key) &&
      (auto_generate_sql_autoincrement || auto_generate_sql_guid_primary))
    query_string.append(" WHERE id = ");

  ptr= new Statement;
  ptr->setLength(query_string.length()+1);
  ptr->setString((char *)malloc(ptr->getLength()));
  if (ptr->getString() == NULL)
  {
    fprintf(stderr, "Memory Allocation error in creating select\n");
    exit(1);
  }
  memset(ptr->getString(), 0, ptr->getLength());
  if ((key) &&
      (auto_generate_sql_autoincrement || auto_generate_sql_guid_primary))
    ptr->setType(SELECT_TYPE_REQUIRES_PREFIX);
  else
    ptr->setType(SELECT_TYPE);
  strcpy(ptr->getString(), query_string.c_str());
  return(ptr);
}

static int
process_options(void)
{
  char *tmp_string;
  struct stat sbuf;
  OptionString *sql_type;
  unsigned int sql_type_count= 0;
  ssize_t bytes_read= 0;
  
  if (user.empty())
    user= "root";

  verbose= opt_verbose.length();

  /* If something is created we clean it up, otherwise we leave schemas alone */
  if ( (!create_string.empty()) || auto_generate_sql)
    opt_preserve= false;

  if (auto_generate_sql && (!create_string.empty() || !user_supplied_query.empty()))
  {
    fprintf(stderr,
            "%s: Can't use --auto-generate-sql when create and query strings are specified!\n",
            internal::my_progname);
    exit(1);
  }

  if (auto_generate_sql && auto_generate_sql_guid_primary &&
      auto_generate_sql_autoincrement)
  {
    fprintf(stderr,
            "%s: Either auto-generate-sql-guid-primary or auto-generate-sql-add-autoincrement can be used!\n",
            internal::my_progname);
    exit(1);
  }

  if (auto_generate_sql && num_of_query && auto_actual_queries)
  {
    fprintf(stderr,
            "%s: Either auto-generate-sql-execute-number or number-of-queries can be used!\n",
            internal::my_progname);
    exit(1);
  }

  parse_comma(!concurrency_str.empty() ? concurrency_str.c_str() : "1", &concurrency);

  if (!opt_csv_str.empty())
  {
    opt_silent= true;

    if (opt_csv_str[0] == '-')
    {
      csv_file= fileno(stdout);
    }
    else
    {
      if ((csv_file= open(opt_csv_str.c_str(), O_CREAT|O_WRONLY|O_APPEND, 
                          S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)) == -1)
      {
        fprintf(stderr,"%s: Could not open csv file: %sn\n",
                internal::my_progname, opt_csv_str.c_str());
        exit(1);
      }
    }
  }

  if (opt_only_print)
    opt_silent= true;

  if (!num_int_cols_opt.empty())
  {
    OptionString *str;
    parse_option(num_int_cols_opt.c_str(), &str, ',');
    num_int_cols= atoi(str->getString());
    if (str->getOption())
      num_int_cols_index= atoi(str->getOption());
    option_cleanup(str);
  }

  if (!num_char_cols_opt.empty())
  {
    OptionString *str;
    parse_option(num_char_cols_opt.c_str(), &str, ',');
    num_char_cols= atoi(str->getString());
    if (str->getOption())
      num_char_cols_index= atoi(str->getOption());
    else
      num_char_cols_index= 0;
    option_cleanup(str);
  }

  if (!num_blob_cols_opt.empty())
  {
    OptionString *str;
    parse_option(num_blob_cols_opt.c_str(), &str, ',');
    num_blob_cols= atoi(str->getString());
    if (str->getOption())
    {
      char *sep_ptr;

      if ((sep_ptr= strchr(str->getOption(), '/')))
      {
        num_blob_cols_size_min= atoi(str->getOption());
        num_blob_cols_size= atoi(sep_ptr+1);
      }
      else
      {
        num_blob_cols_size_min= num_blob_cols_size= atoi(str->getOption());
      }
    }
    else
    {
      num_blob_cols_size= DEFAULT_BLOB_SIZE;
      num_blob_cols_size_min= DEFAULT_BLOB_SIZE;
    }
    option_cleanup(str);
  }


  if (auto_generate_sql)
  {
    uint64_t x= 0;
    Statement *ptr_statement;

    if (verbose >= 2)
      printf("Building Create Statements for Auto\n");

    create_statements= build_table_string();
    /*
      Pre-populate table
    */
    for (ptr_statement= create_statements, x= 0;
         x < auto_generate_sql_unique_write_number;
         x++, ptr_statement= ptr_statement->getNext())
    {
      ptr_statement->setNext(build_insert_string());
    }

    if (verbose >= 2)
      printf("Building Query Statements for Auto\n");

    if (opt_auto_generate_sql_type.empty())
      opt_auto_generate_sql_type= "mixed";

    query_statements_count=
      parse_option(opt_auto_generate_sql_type.c_str(), &query_options, ',');

    query_statements= (Statement **)malloc(sizeof(Statement *) * query_statements_count);
    if (query_statements == NULL)
    {
      fprintf(stderr, "Memory Allocation error in Building Query Statements\n");
      exit(1);
    }
    memset(query_statements, 0, sizeof(Statement *) * query_statements_count);

    sql_type= query_options;
    do
    {
      if (sql_type->getString()[0] == 'r')
      {
        if (verbose >= 2)
          printf("Generating SELECT Statements for Auto\n");

        query_statements[sql_type_count]= build_select_string(false);
        for (ptr_statement= query_statements[sql_type_count], x= 0;
             x < auto_generate_sql_unique_query_number;
             x++, ptr_statement= ptr_statement->getNext())
        {
          ptr_statement->setNext(build_select_string(false));
        }
      }
      else if (sql_type->getString()[0] == 'k')
      {
        if (verbose >= 2)
          printf("Generating SELECT for keys Statements for Auto\n");

        if ( auto_generate_sql_autoincrement == false &&
             auto_generate_sql_guid_primary == false)
        {
          fprintf(stderr,
                  "%s: Can't perform key test without a primary key!\n",
                  internal::my_progname);
          exit(1);
        }

        query_statements[sql_type_count]= build_select_string(true);
        for (ptr_statement= query_statements[sql_type_count], x= 0;
             x < auto_generate_sql_unique_query_number;
             x++, ptr_statement= ptr_statement->getNext())
        {
          ptr_statement->setNext(build_select_string(true));
        }
      }
      else if (sql_type->getString()[0] == 'w')
      {
        /*
          We generate a number of strings in case the engine is
          Archive (since strings which were identical one after another
          would be too easily optimized).
        */
        if (verbose >= 2)
          printf("Generating INSERT Statements for Auto\n");
        query_statements[sql_type_count]= build_insert_string();
        for (ptr_statement= query_statements[sql_type_count], x= 0;
             x < auto_generate_sql_unique_query_number;
             x++, ptr_statement= ptr_statement->getNext())
        {
          ptr_statement->setNext(build_insert_string());
        }
      }
      else if (sql_type->getString()[0] == 'u')
      {
        if ( auto_generate_sql_autoincrement == false &&
             auto_generate_sql_guid_primary == false)
        {
          fprintf(stderr,
                  "%s: Can't perform update test without a primary key!\n",
                  internal::my_progname);
          exit(1);
        }

        query_statements[sql_type_count]= build_update_string();
        for (ptr_statement= query_statements[sql_type_count], x= 0;
             x < auto_generate_sql_unique_query_number;
             x++, ptr_statement= ptr_statement->getNext())
        {
          ptr_statement->setNext(build_update_string());
        }
      }
      else /* Mixed mode is default */
      {
        int coin= 0;

        query_statements[sql_type_count]= build_insert_string();
        /*
          This logic should be extended to do a more mixed load,
          at the moment it results in "every other".
        */
        for (ptr_statement= query_statements[sql_type_count], x= 0;
             x < auto_generate_sql_unique_query_number;
             x++, ptr_statement= ptr_statement->getNext())
        {
          if (coin)
          {
            ptr_statement->setNext(build_insert_string());
            coin= 0;
          }
          else
          {
            ptr_statement->setNext(build_select_string(true));
            coin= 1;
          }
        }
      }
      sql_type_count++;
    } while (sql_type ? (sql_type= sql_type->getNext()) : 0);
  }
  else
  {
    if (!create_string.empty() && !stat(create_string.c_str(), &sbuf))
    {
      int data_file;
      if (!S_ISREG(sbuf.st_mode))
      {
        fprintf(stderr,"%s: Create file was not a regular file\n",
                internal::my_progname);
        exit(1);
      }
      if ((data_file= open(create_string.c_str(), O_RDWR)) == -1)
      {
        fprintf(stderr,"%s: Could not open create file\n", internal::my_progname);
        exit(1);
      }
      if ((uint64_t)(sbuf.st_size + 1) > SIZE_MAX)
      {
        fprintf(stderr, "Request for more memory than architecture supports\n");
        exit(1);
      }
      tmp_string= (char *)malloc((size_t)(sbuf.st_size + 1));
      if (tmp_string == NULL)
      {
        fprintf(stderr, "Memory Allocation error in option processing\n");
        exit(1);
      }
      memset(tmp_string, 0, (size_t)(sbuf.st_size + 1));
      bytes_read= read(data_file, (unsigned char*) tmp_string,
                       (size_t)sbuf.st_size);
      tmp_string[sbuf.st_size]= '\0';
      close(data_file);
      if (bytes_read != sbuf.st_size)
      {
        fprintf(stderr, "Problem reading file: read less bytes than requested\n");
      }
      parse_delimiter(tmp_string, &create_statements, delimiter[0]);
      free(tmp_string);
    }
    else if (!create_string.empty())
    {
      parse_delimiter(create_string.c_str(), &create_statements, delimiter[0]);
    }

    /* Set this up till we fully support options on user generated queries */
    if (!user_supplied_query.empty())
    {
      query_statements_count=
        parse_option("default", &query_options, ',');

      query_statements= (Statement **)malloc(sizeof(Statement *) * query_statements_count);
      if (query_statements == NULL)
      {
        fprintf(stderr, "Memory Allocation error in option processing\n");
        exit(1);
      }
      memset(query_statements, 0, sizeof(Statement *) * query_statements_count); 
    }

    if (!user_supplied_query.empty() && !stat(user_supplied_query.c_str(), &sbuf))
    {
      int data_file;
      if (!S_ISREG(sbuf.st_mode))
      {
        fprintf(stderr,"%s: User query supplied file was not a regular file\n",
                internal::my_progname);
        exit(1);
      }
      if ((data_file= open(user_supplied_query.c_str(), O_RDWR)) == -1)
      {
        fprintf(stderr,"%s: Could not open query supplied file\n", internal::my_progname);
        exit(1);
      }
      if ((uint64_t)(sbuf.st_size + 1) > SIZE_MAX)
      {
        fprintf(stderr, "Request for more memory than architecture supports\n");
        exit(1);
      }
      tmp_string= (char *)malloc((size_t)(sbuf.st_size + 1));
      if (tmp_string == NULL)
      {
        fprintf(stderr, "Memory Allocation error in option processing\n");
        exit(1);
      }
      memset(tmp_string, 0, (size_t)(sbuf.st_size + 1));
      bytes_read= read(data_file, (unsigned char*) tmp_string,
                       (size_t)sbuf.st_size);
      tmp_string[sbuf.st_size]= '\0';
      close(data_file);
      if (bytes_read != sbuf.st_size)
      {
        fprintf(stderr, "Problem reading file: read less bytes than requested\n");
      }
      if (!user_supplied_query.empty())
        actual_queries= parse_delimiter(tmp_string, &query_statements[0],
                                        delimiter[0]);
      free(tmp_string);
    }
    else if (!user_supplied_query.empty())
    {
      actual_queries= parse_delimiter(user_supplied_query.c_str(), &query_statements[0],
                                      delimiter[0]);
    }
  }

  if (!user_supplied_pre_statements.empty()
      && !stat(user_supplied_pre_statements.c_str(), &sbuf))
  {
    int data_file;
    if (!S_ISREG(sbuf.st_mode))
    {
      fprintf(stderr,"%s: User query supplied file was not a regular file\n",
              internal::my_progname);
      exit(1);
    }
    if ((data_file= open(user_supplied_pre_statements.c_str(), O_RDWR)) == -1)
    {
      fprintf(stderr,"%s: Could not open query supplied file\n", internal::my_progname);
      exit(1);
    }
    if ((uint64_t)(sbuf.st_size + 1) > SIZE_MAX)
    {
      fprintf(stderr, "Request for more memory than architecture supports\n");
      exit(1);
    }
    tmp_string= (char *)malloc((size_t)(sbuf.st_size + 1));
    if (tmp_string == NULL)
    {
      fprintf(stderr, "Memory Allocation error in option processing\n");
      exit(1);
    }
    memset(tmp_string, 0, (size_t)(sbuf.st_size + 1));
    bytes_read= read(data_file, (unsigned char*) tmp_string,
                     (size_t)sbuf.st_size);
    tmp_string[sbuf.st_size]= '\0';
    close(data_file);
    if (bytes_read != sbuf.st_size)
    {
      fprintf(stderr, "Problem reading file: read less bytes than requested\n");
    }
    if (!user_supplied_pre_statements.empty())
      (void)parse_delimiter(tmp_string, &pre_statements,
                            delimiter[0]);
    free(tmp_string);
  }
  else if (!user_supplied_pre_statements.empty())
  {
    (void)parse_delimiter(user_supplied_pre_statements.c_str(),
                          &pre_statements,
                          delimiter[0]);
  }

  if (!user_supplied_post_statements.empty()
      && !stat(user_supplied_post_statements.c_str(), &sbuf))
  {
    int data_file;
    if (!S_ISREG(sbuf.st_mode))
    {
      fprintf(stderr,"%s: User query supplied file was not a regular file\n",
              internal::my_progname);
      exit(1);
    }
    if ((data_file= open(user_supplied_post_statements.c_str(), O_RDWR)) == -1)
    {
      fprintf(stderr,"%s: Could not open query supplied file\n", internal::my_progname);
      exit(1);
    }

    if ((uint64_t)(sbuf.st_size + 1) > SIZE_MAX)
    {
      fprintf(stderr, "Request for more memory than architecture supports\n");
      exit(1);
    }
    tmp_string= (char *)malloc((size_t)(sbuf.st_size + 1));
    if (tmp_string == NULL)
    {
      fprintf(stderr, "Memory Allocation error in option processing\n");
      exit(1);
    }
    memset(tmp_string, 0, (size_t)(sbuf.st_size+1));

    bytes_read= read(data_file, (unsigned char*) tmp_string,
                     (size_t)(sbuf.st_size));
    tmp_string[sbuf.st_size]= '\0';
    close(data_file);
    if (bytes_read != sbuf.st_size)
    {
      fprintf(stderr, "Problem reading file: read less bytes than requested\n");
    }
    if (!user_supplied_post_statements.empty())
      (void)parse_delimiter(tmp_string, &post_statements,
                            delimiter[0]);
    free(tmp_string);
  }
  else if (!user_supplied_post_statements.empty())
  {
    (void)parse_delimiter(user_supplied_post_statements.c_str(), &post_statements,
                          delimiter[0]);
  }

  if (verbose >= 2)
    printf("Parsing engines to use.\n");

  if (!default_engine.empty())
    parse_option(default_engine.c_str(), &engine_options, ',');

  if (tty_password)
    opt_password= client_get_tty_password(NULL);
  return(0);
}


static int run_query(drizzle_con_st *con, drizzle_result_st *result,
                     const char *query, int len)
{
  drizzle_return_t ret;
  drizzle_result_st result_buffer;

  if (opt_only_print)
  {
    printf("%.*s;\n", len, query);
    return 0;
  }

  if (verbose >= 3)
    printf("%.*s;\n", len, query);

  if (result == NULL)
    result= &result_buffer;

  result= drizzle_query(con, result, query, len, &ret);

  if (ret == DRIZZLE_RETURN_OK)
    ret= drizzle_result_buffer(result);

  if (result == &result_buffer)
    drizzle_result_free(result);
    
  return ret;
}


static int
generate_primary_key_list(drizzle_con_st *con, OptionString *engine_stmt)
{
  drizzle_result_st result;
  drizzle_row_t row;
  uint64_t counter;


  /*
    Blackhole is a special case, this allows us to test the upper end
    of the server during load runs.
  */
  if (opt_only_print || (engine_stmt &&
                         strstr(engine_stmt->getString(), "blackhole")))
  {
    primary_keys_number_of= 1;
    primary_keys= (char **)malloc((sizeof(char *) *
                                  primary_keys_number_of));
    if (primary_keys == NULL)
    {
      fprintf(stderr, "Memory Allocation error in option processing\n");
      exit(1);
    }
    
    memset(primary_keys, 0, (sizeof(char *) * primary_keys_number_of));
    /* Yes, we strdup a const string to simplify the interface */
    primary_keys[0]= strdup("796c4422-1d94-102a-9d6d-00e0812d");
    if (primary_keys[0] == NULL)
    {
      fprintf(stderr, "Memory Allocation error in option processing\n");
      exit(1);
    }
  }
  else
  {
    if (run_query(con, &result, "SELECT id from t1", strlen("SELECT id from t1")))
    {
      fprintf(stderr,"%s: Cannot select GUID primary keys. (%s)\n", internal::my_progname,
              drizzle_con_error(con));
      exit(1);
    }

    uint64_t num_rows_ret= drizzle_result_row_count(&result);
    if (num_rows_ret > SIZE_MAX)
    {
      fprintf(stderr, "More primary keys than than architecture supports\n");
      exit(1);
    }
    primary_keys_number_of= (size_t)num_rows_ret;

    /* So why check this? Blackhole :) */
    if (primary_keys_number_of)
    {
      /*
        We create the structure and loop and create the items.
      */
      primary_keys= (char **)malloc(sizeof(char *) *
                                    primary_keys_number_of);
      if (primary_keys == NULL)
      {
        fprintf(stderr, "Memory Allocation error in option processing\n");
        exit(1);
      }
      memset(primary_keys, 0, (size_t)(sizeof(char *) * primary_keys_number_of));
      row= drizzle_row_next(&result);
      for (counter= 0; counter < primary_keys_number_of;
           counter++, row= drizzle_row_next(&result))
      {
        primary_keys[counter]= strdup(row[0]);
        if (primary_keys[counter] == NULL)
        {
          fprintf(stderr, "Memory Allocation error in option processing\n");
          exit(1);
        }
      }
    }

    drizzle_result_free(&result);
  }

  return(0);
}

static int
drop_primary_key_list(void)
{
  uint64_t counter;

  if (primary_keys_number_of)
  {
    for (counter= 0; counter < primary_keys_number_of; counter++)
      free(primary_keys[counter]);

    free(primary_keys);
  }

  return 0;
}

static int
create_schema(drizzle_con_st *con, const char *db, Statement *stmt,
              OptionString *engine_stmt, Stats *sptr)
{
  char query[HUGE_STRING_LENGTH];
  Statement *ptr;
  Statement *after_create;
  int len;
  uint64_t count;
  struct timeval start_time, end_time;


  gettimeofday(&start_time, NULL);

  len= snprintf(query, HUGE_STRING_LENGTH, "CREATE SCHEMA `%s`", db);

  if (verbose >= 2)
    printf("Loading Pre-data\n");

  if (run_query(con, NULL, query, len))
  {
    fprintf(stderr,"%s: Cannot create schema %s : %s\n", internal::my_progname, db,
            drizzle_con_error(con));
    exit(1);
  }
  else
  {
    sptr->setCreateCount(sptr->getCreateCount()+1);
  }

  if (opt_only_print)
  {
    printf("use %s;\n", db);
  }
  else
  {
    drizzle_result_st result;
    drizzle_return_t ret;

    if (verbose >= 3)
      printf("%s;\n", query);

    if (drizzle_select_db(con,  &result, db, &ret) == NULL ||
        ret != DRIZZLE_RETURN_OK)
    {
      fprintf(stderr,"%s: Cannot select schema '%s': %s\n",internal::my_progname, db,
              ret == DRIZZLE_RETURN_ERROR_CODE ?
              drizzle_result_error(&result) : drizzle_con_error(con));
      exit(1);
    }
    drizzle_result_free(&result);
    sptr->setCreateCount(sptr->getCreateCount()+1);
  }

  if (engine_stmt)
  {
    len= snprintf(query, HUGE_STRING_LENGTH, "set storage_engine=`%s`",
                  engine_stmt->getString());
    if (run_query(con, NULL, query, len))
    {
      fprintf(stderr,"%s: Cannot set default engine: %s\n", internal::my_progname,
              drizzle_con_error(con));
      exit(1);
    }
    sptr->setCreateCount(sptr->getCreateCount()+1);
  }

  count= 0;
  after_create= stmt;

limit_not_met:
  for (ptr= after_create; ptr && ptr->getLength(); ptr= ptr->getNext(), count++)
  {
    if (auto_generate_sql && ( auto_generate_sql_number == count))
      break;

    if (engine_stmt && engine_stmt->getOption() && ptr->getType() == CREATE_TABLE_TYPE)
    {
      char buffer[HUGE_STRING_LENGTH];

      snprintf(buffer, HUGE_STRING_LENGTH, "%s %s", ptr->getString(),
               engine_stmt->getOption());
      if (run_query(con, NULL, buffer, strlen(buffer)))
      {
        fprintf(stderr,"%s: Cannot run query %.*s ERROR : %s\n",
                internal::my_progname, (uint32_t)ptr->getLength(), ptr->getString(), drizzle_con_error(con));
        if (!opt_ignore_sql_errors)
          exit(1);
      }
      sptr->setCreateCount(sptr->getCreateCount()+1);
    }
    else
    {
      if (run_query(con, NULL, ptr->getString(), ptr->getLength()))
      {
        fprintf(stderr,"%s: Cannot run query %.*s ERROR : %s\n",
                internal::my_progname, (uint32_t)ptr->getLength(), ptr->getString(), drizzle_con_error(con));
        if (!opt_ignore_sql_errors)
          exit(1);
      }
      sptr->setCreateCount(sptr->getCreateCount()+1);
    }
  }

  if (auto_generate_sql && (auto_generate_sql_number > count ))
  {
    /* Special case for auto create, we don't want to create tables twice */
    after_create= stmt->getNext();
    goto limit_not_met;
  }

  gettimeofday(&end_time, NULL);

  sptr->setCreateTiming(timedif(end_time, start_time));

  return(0);
}

static int
drop_schema(drizzle_con_st *con, const char *db)
{
  char query[HUGE_STRING_LENGTH];
  int len;

  len= snprintf(query, HUGE_STRING_LENGTH, "DROP SCHEMA IF EXISTS `%s`", db);

  if (run_query(con, NULL, query, len))
  {
    fprintf(stderr,"%s: Cannot drop database '%s' ERROR : %s\n",
            internal::my_progname, db, drizzle_con_error(con));
    exit(1);
  }



  return(0);
}

static int
run_statements(drizzle_con_st *con, Statement *stmt)
{
  Statement *ptr;

  for (ptr= stmt; ptr && ptr->getLength(); ptr= ptr->getNext())
  {
    if (run_query(con, NULL, ptr->getString(), ptr->getLength()))
    {
      fprintf(stderr,"%s: Cannot run query %.*s ERROR : %s\n",
              internal::my_progname, (uint32_t)ptr->getLength(), ptr->getString(), drizzle_con_error(con));
      exit(1);
    }
  }

  return(0);
}

static int
run_scheduler(Stats *sptr, Statement **stmts, uint32_t concur, uint64_t limit)
{
  uint32_t x;
  uint32_t y;
  unsigned int real_concurrency;
  struct timeval start_time, end_time;
  OptionString *sql_type;
  ThreadContext *con;
  pthread_t mainthread;            /* Thread descriptor */
  pthread_attr_t attr;          /* Thread attributes */


  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr,
                              PTHREAD_CREATE_DETACHED);

  pthread_mutex_lock(&counter_mutex);
  thread_counter= 0;

  pthread_mutex_lock(&sleeper_mutex);
  master_wakeup= 1;
  pthread_mutex_unlock(&sleeper_mutex);

  real_concurrency= 0;

  for (y= 0, sql_type= query_options;
       y < query_statements_count;
       y++, sql_type= sql_type->getNext())
  {
    unsigned int options_loop= 1;

    if (sql_type->getOption())
    {
      options_loop= strtol(sql_type->getOption(),
                           (char **)NULL, 10);
      options_loop= options_loop ? options_loop : 1;
    }

    while (options_loop--)
      for (x= 0; x < concur; x++)
      {
        con= (ThreadContext *)malloc(sizeof(ThreadContext));
        if (con == NULL)
        {
          fprintf(stderr, "Memory Allocation error in scheduler\n");
          exit(1);
        }
        con->setStmt(stmts[y]);
        con->setLimit(limit);

        real_concurrency++;
        /* now you create the thread */
        if (pthread_create(&mainthread, &attr, run_task,
                           (void *)con) != 0)
        {
          fprintf(stderr,"%s: Could not create thread\n", internal::my_progname);
          exit(1);
        }
        thread_counter++;
      }
  }

  /*
    The timer_thread belongs to all threads so it too obeys the wakeup
    call that run tasks obey.
  */
  if (opt_timer_length)
  {
    pthread_mutex_lock(&timer_alarm_mutex);
    timer_alarm= true;
    pthread_mutex_unlock(&timer_alarm_mutex);

    if (pthread_create(&mainthread, &attr, timer_thread,
                       (void *)&opt_timer_length) != 0)
    {
      fprintf(stderr,"%s: Could not create timer thread\n", internal::my_progname);
      exit(1);
    }
  }

  pthread_mutex_unlock(&counter_mutex);
  pthread_attr_destroy(&attr);

  pthread_mutex_lock(&sleeper_mutex);
  master_wakeup= 0;
  pthread_mutex_unlock(&sleeper_mutex);
  pthread_cond_broadcast(&sleep_threshhold);

  gettimeofday(&start_time, NULL);

  /*
    We loop until we know that all children have cleaned up.
  */
  pthread_mutex_lock(&counter_mutex);
  while (thread_counter)
  {
    struct timespec abstime;

    set_timespec(abstime, 3);
    pthread_cond_timedwait(&count_threshhold, &counter_mutex, &abstime);
  }
  pthread_mutex_unlock(&counter_mutex);

  gettimeofday(&end_time, NULL);


  sptr->setTiming(timedif(end_time, start_time));
  sptr->setUsers(concur);
  sptr->setRealUsers(real_concurrency);
  sptr->setRows(limit);

  return(0);
}


pthread_handler_t timer_thread(void *p)
{
  uint32_t *timer_length= (uint32_t *)p;
  struct timespec abstime;


  /*
    We lock around the initial call in case were we in a loop. This
    also keeps the value properly syncronized across call threads.
  */
  pthread_mutex_lock(&sleeper_mutex);
  while (master_wakeup)
  {
    pthread_cond_wait(&sleep_threshhold, &sleeper_mutex);
  }
  pthread_mutex_unlock(&sleeper_mutex);

  set_timespec(abstime, *timer_length);

  pthread_mutex_lock(&timer_alarm_mutex);
  pthread_cond_timedwait(&timer_alarm_threshold, &timer_alarm_mutex, &abstime);
  pthread_mutex_unlock(&timer_alarm_mutex);

  pthread_mutex_lock(&timer_alarm_mutex);
  timer_alarm= false;
  pthread_mutex_unlock(&timer_alarm_mutex);

  return(0);
}

pthread_handler_t run_task(void *p)
{
  uint64_t counter= 0, queries;
  uint64_t detach_counter;
  unsigned int commit_counter;
  drizzle_con_st con;
  drizzle_result_st result;
  drizzle_row_t row;
  Statement *ptr;
  ThreadContext *ctx= (ThreadContext *)p;

  pthread_mutex_lock(&sleeper_mutex);
  while (master_wakeup)
  {
    pthread_cond_wait(&sleep_threshhold, &sleeper_mutex);
  }
  pthread_mutex_unlock(&sleeper_mutex);

  slap_connect(&con, true);

  if (verbose >= 3)
    printf("connected!\n");
  queries= 0;

  commit_counter= 0;
  if (commit_rate)
    run_query(&con, NULL, "SET AUTOCOMMIT=0", strlen("SET AUTOCOMMIT=0"));

limit_not_met:
  for (ptr= ctx->getStmt(), detach_counter= 0;
       ptr && ptr->getLength();
       ptr= ptr->getNext(), detach_counter++)
  {
    if (!opt_only_print && detach_rate && !(detach_counter % detach_rate))
    {
      slap_close(&con);
      slap_connect(&con, true);
    }

    /*
      We have to execute differently based on query type. This should become a function.
    */
    if ((ptr->getType() == UPDATE_TYPE_REQUIRES_PREFIX) ||
        (ptr->getType() == SELECT_TYPE_REQUIRES_PREFIX))
    {
      int length;
      unsigned int key_val;
      char *key;
      char buffer[HUGE_STRING_LENGTH];

      /*
        This should only happen if some sort of new engine was
        implemented that didn't properly handle UPDATEs.

        Just in case someone runs this under an experimental engine we don't
        want a crash so the if() is placed here.
      */
      assert(primary_keys_number_of);
      if (primary_keys_number_of)
      {
        key_val= (unsigned int)(random() % primary_keys_number_of);
        key= primary_keys[key_val];

        assert(key);

        length= snprintf(buffer, HUGE_STRING_LENGTH, "%.*s '%s'",
                         (int)ptr->getLength(), ptr->getString(), key);

        if (run_query(&con, &result, buffer, length))
        {
          fprintf(stderr,"%s: Cannot run query %.*s ERROR : %s\n",
                  internal::my_progname, (uint32_t)length, buffer, drizzle_con_error(&con));
          exit(1);
        }
      }
    }
    else
    {
      if (run_query(&con, &result, ptr->getString(), ptr->getLength()))
      {
        fprintf(stderr,"%s: Cannot run query %.*s ERROR : %s\n",
                internal::my_progname, (uint32_t)ptr->getLength(), ptr->getString(), drizzle_con_error(&con));
        exit(1);
      }
    }

    if (!opt_only_print)
    {
      while ((row = drizzle_row_next(&result)))
        counter++;
      drizzle_result_free(&result);
    }
    queries++;

    if (commit_rate && (++commit_counter == commit_rate))
    {
      commit_counter= 0;
      run_query(&con, NULL, "COMMIT", strlen("COMMIT"));
    }

    /* If the timer is set, and the alarm is not active then end */
    if (opt_timer_length && timer_alarm == false)
      goto end;

    /* If limit has been reached, and we are not in a timer_alarm just end */
    if (ctx->getLimit() && queries == ctx->getLimit() && timer_alarm == false)
      goto end;
  }

  if (opt_timer_length && timer_alarm == true)
    goto limit_not_met;

  if (ctx->getLimit() && queries < ctx->getLimit())
    goto limit_not_met;


end:
  if (commit_rate)
    run_query(&con, NULL, "COMMIT", strlen("COMMIT"));

  slap_close(&con);

  pthread_mutex_lock(&counter_mutex);
  thread_counter--;
  pthread_cond_signal(&count_threshhold);
  pthread_mutex_unlock(&counter_mutex);

  free(ctx);

  return(0);
}

/*
  Parse records from comma seperated string. : is a reserved character and is used for options
  on variables.
*/
uint
parse_option(const char *origin, OptionString **stmt, char delm)
{
  char *string;
  char *begin_ptr;
  char *end_ptr;
  OptionString **sptr= stmt;
  OptionString *tmp;
  uint32_t length= strlen(origin);
  uint32_t count= 0; /* We know that there is always one */

  end_ptr= (char *)origin + length;

  tmp= *sptr= (OptionString *)malloc(sizeof(OptionString));
  if (tmp == NULL)
  {
    fprintf(stderr,"Error allocating memory while parsing options\n");
    exit(1);
  }
  memset(tmp, 0, sizeof(OptionString));

  for (begin_ptr= (char *)origin;
       begin_ptr != end_ptr;
       tmp= tmp->getNext())
  {
    char buffer[HUGE_STRING_LENGTH];
    char *buffer_ptr;

    memset(buffer, 0, HUGE_STRING_LENGTH);

    string= strchr(begin_ptr, delm);

    if (string)
    {
      memcpy(buffer, begin_ptr, string - begin_ptr);
      begin_ptr= string+1;
    }
    else
    {
      size_t begin_len= strlen(begin_ptr);
      memcpy(buffer, begin_ptr, begin_len);
      begin_ptr= end_ptr;
    }

    if ((buffer_ptr= strchr(buffer, ':')))
    {
      /* Set a null so that we can get strlen() correct later on */
      buffer_ptr[0]= 0;
      buffer_ptr++;

      /* Move past the : and the first string */
      tmp->setOptionLength(strlen(buffer_ptr));
      tmp->setOption((char *)malloc(tmp->getOptionLength() + 1));
      if (tmp->getOption() == NULL)
      {
        fprintf(stderr,"Error allocating memory while parsing options\n");
        exit(1);
      }
      memcpy(tmp->getOption(), buffer_ptr, tmp->getOptionLength());
      tmp->setOption(tmp->getOptionLength(),0); 
    }

    tmp->setLength(strlen(buffer));
    tmp->setString(strdup(buffer));
    if (tmp->getString() == NULL)
    {
      fprintf(stderr,"Error allocating memory while parsing options\n");
      exit(1);
    }

    if (isspace(*begin_ptr))
      begin_ptr++;

    count++;

    if (begin_ptr != end_ptr)
    {
      tmp->setNext((OptionString *)malloc(sizeof(OptionString)));
      if (tmp->getNext() == NULL)
      {
        fprintf(stderr,"Error allocating memory while parsing options\n");
        exit(1);
      }
      memset(tmp->getNext(), 0, sizeof(OptionString));
    }
    
  }

  return count;
}


/*
  Raw parsing interface. If you want the slap specific parser look at
  parse_option.
*/
uint
parse_delimiter(const char *script, Statement **stmt, char delm)
{
  char *retstr;
  char *ptr= (char *)script;
  Statement **sptr= stmt;
  Statement *tmp;
  uint32_t length= strlen(script);
  uint32_t count= 0; /* We know that there is always one */

  for (tmp= *sptr= (Statement *)calloc(1, sizeof(Statement));
       (retstr= strchr(ptr, delm));
       tmp->setNext((Statement *)calloc(1, sizeof(Statement))),
       tmp= tmp->getNext())
  {
    if (tmp == NULL)
    {
      fprintf(stderr,"Error allocating memory while parsing delimiter\n");
      exit(1);
    }

    count++;
    tmp->setLength((size_t)(retstr - ptr));
    tmp->setString((char *)malloc(tmp->getLength() + 1));

    if (tmp->getString() == NULL)
    {
      fprintf(stderr,"Error allocating memory while parsing delimiter\n");
      exit(1);
    }

    memcpy(tmp->getString(), ptr, tmp->getLength());
    tmp->setString(tmp->getLength(), 0);
    ptr+= retstr - ptr + 1;
    if (isspace(*ptr))
      ptr++;
  }

  if (ptr != script+length)
  {
    tmp->setLength((size_t)((script + length) - ptr));
    tmp->setString((char *)malloc(tmp->getLength() + 1));
    if (tmp->getString() == NULL)
    {
      fprintf(stderr,"Error allocating memory while parsing delimiter\n");
      exit(1);
    }
    memcpy(tmp->getString(), ptr, tmp->getLength());
    tmp->setString(tmp->getLength(),0);
    count++;
  }

  return count;
}


/*
  Parse comma is different from parse_delimeter in that it parses
  number ranges from a comma seperated string.
  In restrospect, this is a lousy name from this function.
*/
uint
parse_comma(const char *string, uint32_t **range)
{
  unsigned int count= 1,x; /* We know that there is always one */
  char *retstr;
  char *ptr= (char *)string;
  unsigned int *nptr;

  for (;*ptr; ptr++)
    if (*ptr == ',') count++;

  /* One extra spot for the NULL */
  nptr= *range= (uint32_t *)malloc(sizeof(unsigned int) * (count + 1));
  memset(nptr, 0, sizeof(unsigned int) * (count + 1));

  ptr= (char *)string;
  x= 0;
  while ((retstr= strchr(ptr,',')))
  {
    nptr[x++]= atoi(ptr);
    ptr+= retstr - ptr + 1;
  }
  nptr[x++]= atoi(ptr);

  return count;
}

void
print_conclusions(Conclusions *con)
{
  printf("Benchmark\n");
  if (con->getEngine())
    printf("\tRunning for engine %s\n", con->getEngine());
  if (!opt_label.empty() || !opt_auto_generate_sql_type.empty())
  {
    const char *ptr= opt_auto_generate_sql_type.c_str() ? opt_auto_generate_sql_type.c_str() : "query";
    printf("\tLoad: %s\n", !opt_label.empty() ? opt_label.c_str() : ptr);
  }
  printf("\tAverage Time took to generate schema and initial data: %ld.%03ld seconds\n",
         con->getCreateAvgTiming() / 1000, con->getCreateAvgTiming() % 1000);
  printf("\tAverage number of seconds to run all queries: %ld.%03ld seconds\n",
         con->getAvgTiming() / 1000, con->getAvgTiming() % 1000);
  printf("\tMinimum number of seconds to run all queries: %ld.%03ld seconds\n",
         con->getMinTiming() / 1000, con->getMinTiming() % 1000);
  printf("\tMaximum number of seconds to run all queries: %ld.%03ld seconds\n",
         con->getMaxTiming() / 1000, con->getMaxTiming() % 1000);
  printf("\tTotal time for tests: %ld.%03ld seconds\n",
         con->getSumOfTime() / 1000, con->getSumOfTime() % 1000);
  printf("\tStandard Deviation: %ld.%03ld\n", con->getStdDev() / 1000, con->getStdDev() % 1000);
  printf("\tNumber of queries in create queries: %"PRIu64"\n", con->getCreateCount());
  printf("\tNumber of clients running queries: %u/%u\n",
         con->getUsers(), con->getRealUsers());
  printf("\tNumber of times test was run: %u\n", iterations);
  printf("\tAverage number of queries per client: %"PRIu64"\n", con->getAvgRows());
  printf("\n");
}

void
print_conclusions_csv(Conclusions *con)
{
  unsigned int x;
  char buffer[HUGE_STRING_LENGTH];
  char label_buffer[HUGE_STRING_LENGTH];
  size_t string_len;
  const char *temp_label= opt_label.c_str();

  memset(label_buffer, 0, sizeof(label_buffer));

  if (!opt_label.empty())
  {
    string_len= opt_label.length();

    for (x= 0; x < string_len; x++)
    {
      if (temp_label[x] == ',')
        label_buffer[x]= '-';
      else
        label_buffer[x]= temp_label[x] ;
    }
  }
  else if (!opt_auto_generate_sql_type.empty())
  {
    string_len= opt_auto_generate_sql_type.length();

    for (x= 0; x < string_len; x++)
    {
      if (opt_auto_generate_sql_type[x] == ',')
        label_buffer[x]= '-';
      else
        label_buffer[x]= opt_auto_generate_sql_type[x] ;
    }
  }
  else
  {
    snprintf(label_buffer, HUGE_STRING_LENGTH, "query");
  }

  snprintf(buffer, HUGE_STRING_LENGTH,
           "%s,%s,%ld.%03ld,%ld.%03ld,%ld.%03ld,%ld.%03ld,%ld.%03ld,"
           "%u,%u,%u,%"PRIu64"\n",
           con->getEngine() ? con->getEngine() : "", /* Storage engine we ran against */
           label_buffer, /* Load type */
           con->getAvgTiming() / 1000, con->getAvgTiming() % 1000, /* Time to load */
           con->getMinTiming() / 1000, con->getMinTiming() % 1000, /* Min time */
           con->getMaxTiming() / 1000, con->getMaxTiming() % 1000, /* Max time */
           con->getSumOfTime() / 1000, con->getSumOfTime() % 1000, /* Total time */
           con->getStdDev() / 1000, con->getStdDev() % 1000, /* Standard Deviation */
           iterations, /* Iterations */
           con->getUsers(), /* Children used max_timing */
           con->getRealUsers(), /* Children used max_timing */
           con->getAvgRows()  /* Queries run */
           );
  write(csv_file, (unsigned char*) buffer, (uint32_t)strlen(buffer));
}

void
generate_stats(Conclusions *con, OptionString *eng, Stats *sptr)
{
  Stats *ptr;
  unsigned int x;

  con->setMinTiming(sptr->getTiming());
  con->setMaxTiming(sptr->getTiming());
  con->setMinRows(sptr->getRows());
  con->setMaxRows(sptr->getRows());

  /* At the moment we assume uniform */
  con->setUsers(sptr->getUsers());
  con->setRealUsers(sptr->getRealUsers());
  con->setAvgRows(sptr->getRows());

  /* With no next, we know it is the last element that was malloced */
  for (ptr= sptr, x= 0; x < iterations; ptr++, x++)
  {
    con->setAvgTiming(ptr->getTiming()+con->getAvgTiming());

    if (ptr->getTiming() > con->getMaxTiming())
      con->setMaxTiming(ptr->getTiming());
    if (ptr->getTiming() < con->getMinTiming())
      con->setMinTiming(ptr->getTiming());
  }
  con->setSumOfTime(con->getAvgTiming());
  con->setAvgTiming(con->getAvgTiming()/iterations);

  if (eng && eng->getString())
    con->setEngine(eng->getString());
  else
    con->setEngine(NULL);

  standard_deviation(con, sptr);

  /* Now we do the create time operations */
  con->setCreateMinTiming(sptr->getCreateTiming());
  con->setCreateMaxTiming(sptr->getCreateTiming());

  /* At the moment we assume uniform */
  con->setCreateCount(sptr->getCreateCount());

  /* With no next, we know it is the last element that was malloced */
  for (ptr= sptr, x= 0; x < iterations; ptr++, x++)
  {
    con->setCreateAvgTiming(ptr->getCreateTiming()+con->getCreateAvgTiming());

    if (ptr->getCreateTiming() > con->getCreateMaxTiming())
      con->setCreateMaxTiming(ptr->getCreateTiming());
    if (ptr->getCreateTiming() < con->getCreateMinTiming())
      con->setCreateMinTiming(ptr->getCreateTiming());
  }
  con->setCreateAvgTiming(con->getCreateAvgTiming()/iterations);
}

void
option_cleanup(OptionString *stmt)
{
  OptionString *ptr, *nptr;
  if (!stmt)
    return;

  for (ptr= stmt; ptr; ptr= nptr)
  {
    nptr= ptr->getNext();
    if (ptr->getString())
      free(ptr->getString());
    if (ptr->getOption())
      free(ptr->getOption());
    free(ptr);
  }
}

void
statement_cleanup(Statement *stmt)
{
  Statement *ptr, *nptr;
  if (!stmt)
    return;

  for (ptr= stmt; ptr; ptr= nptr)
  {
    nptr= ptr->getNext();
    delete ptr;
  }
}

void
slap_close(drizzle_con_st *con)
{
  if (opt_only_print)
    return;

  drizzle_free(drizzle_con_drizzle(con));
}

void
slap_connect(drizzle_con_st *con, bool connect_to_schema)
{
  /* Connect to server */
  static uint32_t connection_retry_sleep= 100000; /* Microseconds */
  int x, connect_error= 1;
  drizzle_return_t ret;
  drizzle_st *drizzle;

  if (opt_only_print)
    return;

  if (opt_delayed_start)
    usleep(random()%opt_delayed_start);

  if ((drizzle= drizzle_create(NULL)) == NULL ||
      drizzle_con_add_tcp(drizzle, con, host.c_str(), opt_drizzle_port, user.c_str(),
                          opt_password.c_str(),
                          connect_to_schema ? create_schema_string.c_str() : NULL,
                          opt_mysql ? DRIZZLE_CON_MYSQL : DRIZZLE_CON_NONE) == NULL)
  {
    fprintf(stderr,"%s: Error creating drizzle object\n", internal::my_progname);
    exit(1);
  }

  for (x= 0; x < 10; x++)
  {
    if ((ret= drizzle_con_connect(con)) == DRIZZLE_RETURN_OK)
    {
      /* Connect suceeded */
      connect_error= 0;
      break;
    }
    usleep(connection_retry_sleep);
  }
  if (connect_error)
  {
    fprintf(stderr,"%s: Error when connecting to server: %d %s\n", internal::my_progname,
            ret, drizzle_con_error(con));
    exit(1);
  }

  return;
}

void
standard_deviation (Conclusions *con, Stats *sptr)
{
  unsigned int x;
  long int sum_of_squares;
  double the_catch;
  Stats *ptr;

  if (iterations == 1 || iterations == 0)
  {
    con->setStdDev(0);
    return;
  }

  for (ptr= sptr, x= 0, sum_of_squares= 0; x < iterations; ptr++, x++)
  {
    long int deviation;

    deviation= ptr->getTiming() - con->getAvgTiming();
    sum_of_squares+= deviation*deviation;
  }

  the_catch= sqrt((double)(sum_of_squares/(iterations -1)));
  con->setStdDev((long int)the_catch);
}
