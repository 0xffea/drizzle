/* Copyright (C) 2000-2004 DRIZZLE AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation.

   There are special exceptions to the terms and conditions of the GPL as it
   is applied to this software. View the full text of the exception in file
   EXCEPTIONS-CLIENT in the directory of this software distribution.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include <drizzled/global.h>
#include <mysys/my_sys.h>
#include "my_time.h"
#include <mysys/mysys_err.h>
#include "drizzle.h"
#include "errmsg.h"
#include <vio/violite.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#ifdef   HAVE_PWD_H
#include <pwd.h>
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifdef HAVE_SELECT_H
#include <select.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_POLL
#include <sys/poll.h>
#endif
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#include <mysys/my_pthread.h>        /* because of signal()  */
#ifndef INADDR_NONE
#define INADDR_NONE  -1
#endif

#include <sql_common.h>
#include "client_settings.h"
#include <drizzled/version.h>

#undef net_buffer_length
#undef max_allowed_packet

uint32_t     net_buffer_length= 8192;
uint32_t    max_allowed_packet= 1024L*1024L*1024L;

#include <errno.h>
#define SOCKET_ERROR -1

/*
  If allowed through some configuration, then this needs to
  be changed
*/
#define MAX_LONG_DATA_LENGTH 8192
#define unsigned_field(A) ((A)->flags & UNSIGNED_FLAG)


static DRIZZLE_PARAMETERS drizzle_internal_parameters=
{&max_allowed_packet, &net_buffer_length, 0};

const DRIZZLE_PARAMETERS *STDCALL drizzle_get_parameters(void)
{
  return &drizzle_internal_parameters;
}

bool STDCALL drizzle_thread_init()
{
  return my_thread_init();
}

void STDCALL drizzle_thread_end()
{
  my_thread_end();
}


/*
  Expand wildcard to a sql string
*/

static void
append_wild(char *to, char *end, const char *wild)
{
  end-=5;          /* Some extra */
  if (wild && wild[0])
  {
    to= strcpy(to," like '");
    to+= 7; /* strlen(" like '"); */

    while (*wild && to < end)
    {
      if (*wild == '\\' || *wild == '\'')
  *to++='\\';
      *to++= *wild++;
    }
    if (*wild)          /* Too small buffer */
      *to++='%';        /* Nicer this way */
    to[0]='\'';
    to[1]=0;
  }
}

/**************************************************************************
  Ignore SIGPIPE handler
   ARGSUSED
**************************************************************************/

sig_handler
my_pipe_sig_handler(int sig __attribute__((unused)))
{
#ifdef DONT_REMEMBER_SIGNAL
  (void) signal(SIGPIPE, my_pipe_sig_handler);
#endif
}


/**************************************************************************
  Change user and database
**************************************************************************/

int cli_read_change_user_result(DRIZZLE *drizzle)
{
  ulong pkt_length;

  pkt_length= cli_safe_read(drizzle);
  
  if (pkt_length == packet_error)
    return 1;

  return 0;
}

bool STDCALL drizzle_change_user(DRIZZLE *drizzle, const char *user,
                                 const char *passwd, const char *db)
{
  char buff[USERNAME_LENGTH+SCRAMBLED_PASSWORD_CHAR_LENGTH+NAME_LEN+2];
  char *end= buff;
  int rc;
  const CHARSET_INFO *saved_cs= drizzle->charset;

  /* Get the connection-default character set. */

  if (drizzle_init_character_set(drizzle))
  {
    drizzle->charset= saved_cs;
    return(true);
  }

  /* Use an empty string instead of NULL. */

  if (!user)
    user="";
  if (!passwd)
    passwd="";

  /* Store user into the buffer */
  end= strncpy(end, user, USERNAME_LENGTH) + USERNAME_LENGTH + 1;

  /* write scrambled password according to server capabilities */
  if (passwd[0])
  {
    {
      *end++= SCRAMBLE_LENGTH;
      end+= SCRAMBLE_LENGTH;
    }
  }
  else
    *end++= '\0';                               /* empty password */
  /* Add database if needed */
  end= strncpy(end, db ? db : "", NAME_LEN) + NAME_LEN + 1;

  /* Add character set number. */

  if (drizzle->server_capabilities & CLIENT_SECURE_CONNECTION)
  {
    int2store(end, (ushort) drizzle->charset->number);
    end+= 2;
  }

  /* Write authentication package */
  (void)simple_command(drizzle,COM_CHANGE_USER, (uchar*) buff, (ulong) (end-buff), 1);

  rc= (*drizzle->methods->read_change_user_result)(drizzle);

  if (rc == 0)
  {
    /* Free old connect information */
    if(drizzle->user)
      free(drizzle->user);
    if(drizzle->passwd)
      free(drizzle->passwd);
    if(drizzle->db)
      free(drizzle->db);

    /* alloc new connect information */
    drizzle->user= strdup(user);
    drizzle->passwd= strdup(passwd);
    drizzle->db= db ? strdup(db) : 0;
  }
  else
  {
    drizzle->charset= saved_cs;
  }

  return(rc);
}

#if defined(HAVE_GETPWUID) && defined(NO_GETPWUID_DECL)
struct passwd *getpwuid(uid_t);
char* getlogin(void);
#endif

/**************************************************************************
  Do a query. If query returned rows, free old rows.
  Read data by drizzle_store_result or by repeat call of drizzle_fetch_row
**************************************************************************/

int STDCALL
drizzle_query(DRIZZLE *drizzle, const char *query)
{
  return drizzle_real_query(drizzle,query, (uint) strlen(query));
}


/**************************************************************************
  Return next field of the query results
**************************************************************************/

DRIZZLE_FIELD * STDCALL
drizzle_fetch_field(DRIZZLE_RES *result)
{
  if (result->current_field >= result->field_count)
    return(NULL);
  return &result->fields[result->current_field++];
}


/**************************************************************************
  Move to a specific row and column
**************************************************************************/

void STDCALL
drizzle_data_seek(DRIZZLE_RES *result, uint64_t row)
{
  DRIZZLE_ROWS  *tmp=0;
  if (result->data)
    for (tmp=result->data->data; row-- && tmp ; tmp = tmp->next) ;
  result->current_row=0;
  result->data_cursor = tmp;
}


/*************************************************************************
  put the row or field cursor one a position one got from DRIZZLE_ROW_tell()
  This doesn't restore any data. The next drizzle_fetch_row or
  drizzle_fetch_field will return the next row or field after the last used
*************************************************************************/

DRIZZLE_ROW_OFFSET STDCALL
drizzle_row_seek(DRIZZLE_RES *result, DRIZZLE_ROW_OFFSET row)
{
  DRIZZLE_ROW_OFFSET return_value=result->data_cursor;
  result->current_row= 0;
  result->data_cursor= row;
  return return_value;
}


DRIZZLE_FIELD_OFFSET STDCALL
drizzle_field_seek(DRIZZLE_RES *result, DRIZZLE_FIELD_OFFSET field_offset)
{
  DRIZZLE_FIELD_OFFSET return_value=result->current_field;
  result->current_field=field_offset;
  return return_value;
}


/*****************************************************************************
  List all tables in a database
  If wild is given then only the tables matching wild is returned
*****************************************************************************/

DRIZZLE_RES * STDCALL
drizzle_list_tables(DRIZZLE *drizzle, const char *wild)
{
  char buff[255];
  char *ptr= strcpy(buff, "show tables");
  ptr+= 11; /* strlen("show tables"); */

  append_wild(ptr,buff+sizeof(buff),wild);
  if (drizzle_query(drizzle,buff))
    return(0);
  return (drizzle_store_result(drizzle));
}


DRIZZLE_FIELD *cli_list_fields(DRIZZLE *drizzle)
{
  DRIZZLE_DATA *query;
  if (!(query= cli_read_rows(drizzle,(DRIZZLE_FIELD*) 0, 8)))
    return NULL;

  drizzle->field_count= (uint) query->rows;
  return unpack_fields(query,&drizzle->field_alloc, drizzle->field_count, 1);
}


/**************************************************************************
  List all fields in a table
  If wild is given then only the fields matching wild is returned
  Instead of this use query:
  show fields in 'table' like "wild"
**************************************************************************/

DRIZZLE_RES * STDCALL
drizzle_list_fields(DRIZZLE *drizzle, const char *table, const char *wild)
{
  DRIZZLE_RES   *result;
  DRIZZLE_FIELD *fields;
  char buff[257], *end;

  end= strncpy(buff, table, 128) + 128;
  end= strncpy(end+1, wild ? wild : "", 128) + 128;

  free_old_query(drizzle);
  if (simple_command(drizzle, COM_FIELD_LIST, (uchar*) buff,
                     (ulong) (end-buff), 1) ||
      !(fields= (*drizzle->methods->list_fields)(drizzle)))
    return(NULL);

  if (!(result = (DRIZZLE_RES *) malloc(sizeof(DRIZZLE_RES))))
    return(NULL);

  result->methods= drizzle->methods;
  result->field_alloc=drizzle->field_alloc;
  drizzle->fields=0;
  result->field_count = drizzle->field_count;
  result->fields= fields;
  result->eof=1;
  return(result);
}

/* List all running processes (threads) in server */

DRIZZLE_RES * STDCALL
drizzle_list_processes(DRIZZLE *drizzle)
{
  DRIZZLE_DATA *fields;
  uint field_count;
  uchar *pos;

  if (simple_command(drizzle,COM_PROCESS_INFO,0,0,0))
    return(0);
  free_old_query(drizzle);
  pos=(uchar*) drizzle->net.read_pos;
  field_count=(uint) net_field_length(&pos);
  if (!(fields = (*drizzle->methods->read_rows)(drizzle,(DRIZZLE_FIELD*) 0, 7)))
    return(NULL);
  if (!(drizzle->fields=unpack_fields(fields,&drizzle->field_alloc,field_count,0)))
    return(0);
  drizzle->status=DRIZZLE_STATUS_GET_RESULT;
  drizzle->field_count=field_count;
  return(drizzle_store_result(drizzle));
}


int STDCALL
drizzle_shutdown(DRIZZLE *drizzle, enum drizzle_enum_shutdown_level shutdown_level)
{
  uchar level[1];
  level[0]= (uchar) shutdown_level;
  return(simple_command(drizzle, COM_SHUTDOWN, level, 1, 0));
}


int STDCALL
drizzle_refresh(DRIZZLE *drizzle,uint options)
{
  uchar bits[1];
  bits[0]= (uchar) options;
  return(simple_command(drizzle, COM_REFRESH, bits, 1, 0));
}


int32_t STDCALL
drizzle_kill(DRIZZLE *drizzle, uint32_t pid)
{
  uchar buff[4];
  int4store(buff,pid);
  return(simple_command(drizzle,COM_PROCESS_KILL,buff,sizeof(buff),0));
}


int STDCALL
drizzle_set_server_option(DRIZZLE *drizzle, enum enum_drizzle_set_option option)
{
  uchar buff[2];
  int2store(buff, (uint) option);
  return(simple_command(drizzle, COM_SET_OPTION, buff, sizeof(buff), 0));
}


const char *cli_read_statistics(DRIZZLE *drizzle)
{
  drizzle->net.read_pos[drizzle->packet_length]=0;  /* End of stat string */
  if (!drizzle->net.read_pos[0])
  {
    set_drizzle_error(drizzle, CR_WRONG_HOST_INFO, unknown_sqlstate);
    return drizzle->net.last_error;
  }
  return (char*) drizzle->net.read_pos;
}


int STDCALL
drizzle_ping(DRIZZLE *drizzle)
{
  int res;
  res= simple_command(drizzle,COM_PING,0,0,0);
  if (res == CR_SERVER_LOST && drizzle->reconnect)
    res= simple_command(drizzle,COM_PING,0,0,0);
  return(res);
}


const char * STDCALL
drizzle_get_server_info(const DRIZZLE *drizzle)
{
  return((char*) drizzle->server_version);
}


const char * STDCALL
drizzle_get_host_info(const DRIZZLE *drizzle)
{
  return(drizzle->host_info);
}


uint STDCALL
drizzle_get_proto_info(const DRIZZLE *drizzle)
{
  return (drizzle->protocol_version);
}

const char * STDCALL
drizzle_get_client_info(void)
{
  return (char*) MYSQL_SERVER_VERSION;
}

uint32_t STDCALL drizzle_get_client_version(void)
{
  return MYSQL_VERSION_ID;
}

bool STDCALL drizzle_eof(const DRIZZLE_RES *res)
{
  return res->eof;
}

const DRIZZLE_FIELD * STDCALL drizzle_fetch_field_direct(const DRIZZLE_RES *res, unsigned int fieldnr)
{
  return &(res)->fields[fieldnr];
}

const DRIZZLE_FIELD * STDCALL drizzle_fetch_fields(const DRIZZLE_RES *res)
{
  return res->fields;
}

DRIZZLE_ROW_OFFSET STDCALL drizzle_row_tell(const DRIZZLE_RES *res)
{
  return res->data_cursor;
}

DRIZZLE_FIELD_OFFSET STDCALL drizzle_field_tell(const DRIZZLE_RES *res)
{
  return res->current_field;
}

/* DRIZZLE */

unsigned int STDCALL drizzle_field_count(const DRIZZLE *drizzle)
{
  return drizzle->field_count;
}

uint64_t STDCALL drizzle_affected_rows(const DRIZZLE *drizzle)
{
  return drizzle->affected_rows;
}

uint64_t STDCALL drizzle_insert_id(const DRIZZLE *drizzle)
{
  return drizzle->insert_id;
}

const char *STDCALL drizzle_sqlstate(const DRIZZLE *drizzle)
{
  return drizzle ? drizzle->net.sqlstate : cant_connect_sqlstate;
}

uint32_t STDCALL drizzle_warning_count(const DRIZZLE *drizzle)
{
  return drizzle->warning_count;
}

const char *STDCALL drizzle_info(const DRIZZLE *drizzle)
{
  return drizzle->info;
}

uint32_t STDCALL drizzle_thread_id(const DRIZZLE *drizzle)
{
  return drizzle->thread_id;
}

const char * STDCALL drizzle_character_set_name(const DRIZZLE *drizzle)
{
  return drizzle->charset->csname;
}

void STDCALL drizzle_get_character_set_info(const DRIZZLE *drizzle, MY_CHARSET_INFO *csinfo)
{
  csinfo->number   = drizzle->charset->number;
  csinfo->state    = drizzle->charset->state;
  csinfo->csname   = drizzle->charset->csname;
  csinfo->name     = drizzle->charset->name;
  csinfo->comment  = drizzle->charset->comment;
  csinfo->mbminlen = drizzle->charset->mbminlen;
  csinfo->mbmaxlen = drizzle->charset->mbmaxlen;

  if (drizzle->options.charset_dir)
    csinfo->dir = drizzle->options.charset_dir;
  else
    csinfo->dir = charsets_dir;
}

uint STDCALL drizzle_thread_safe(void)
{
  return 1;
}


bool STDCALL drizzle_embedded(void)
{
#ifdef EMBEDDED_LIBRARY
  return true;
#else
  return false;
#endif
}

/****************************************************************************
  Some support functions
****************************************************************************/

/*
  Functions called my my_net_init() to set some application specific variables
*/

void my_net_local_init(NET *net)
{
  net->max_packet=   (uint) net_buffer_length;
  my_net_set_read_timeout(net, CLIENT_NET_READ_TIMEOUT);
  my_net_set_write_timeout(net, CLIENT_NET_WRITE_TIMEOUT);
  net->retry_count=  1;
  net->max_packet_size= max(net_buffer_length, max_allowed_packet);
}

/*
  This function is used to create HEX string that you
  can use in a SQL statement in of the either ways:
    INSERT INTO blob_column VALUES (0xAABBCC);  (any DRIZZLE version)
    INSERT INTO blob_column VALUES (X'AABBCC'); 
  
  The string in "from" is encoded to a HEX string.
  The result is placed in "to" and a terminating null byte is appended.
  
  The string pointed to by "from" must be "length" bytes long.
  You must allocate the "to" buffer to be at least length*2+1 bytes long.
  Each character needs two bytes, and you need room for the terminating
  null byte. When drizzle_hex_string() returns, the contents of "to" will
  be a null-terminated string. The return value is the length of the
  encoded string, not including the terminating null character.

  The return value does not contain any leading 0x or a leading X' and
  trailing '. The caller must supply whichever of those is desired.
*/

uint32_t STDCALL
drizzle_hex_string(char *to, const char *from, uint32_t length)
{
  char *to0= to;
  const char *end;
            
  for (end= from + length; from < end; from++)
  {
    *to++= _dig_vec_upper[((unsigned char) *from) >> 4];
    *to++= _dig_vec_upper[((unsigned char) *from) & 0x0F];
  }
  *to= '\0';
  return (uint32_t) (to-to0);
}

/*
  Add escape characters to a string (blob?) to make it suitable for a insert
  to should at least have place for length*2+1 chars
  Returns the length of the to string
*/

uint32_t STDCALL
drizzle_escape_string(char *to,const char *from, uint32_t length)
{
  return escape_string_for_drizzle(default_charset_info, to, 0, from, length);
}

uint32_t STDCALL
drizzle_real_escape_string(DRIZZLE *drizzle, char *to,const char *from,
       uint32_t length)
{
  if (drizzle->server_status & SERVER_STATUS_NO_BACKSLASH_ESCAPES)
    return escape_quotes_for_drizzle(drizzle->charset, to, 0, from, length);
  return escape_string_for_drizzle(drizzle->charset, to, 0, from, length);
}

void STDCALL
myodbc_remove_escape(const DRIZZLE *drizzle, char *name)
{
  char *to;
#ifdef USE_MB
  bool use_mb_flag= use_mb(drizzle->charset);
  char *end=NULL;
  if (use_mb_flag)
    for (end=name; *end ; end++) ;
#endif

  for (to=name ; *name ; name++)
  {
#ifdef USE_MB
    int l;
    if (use_mb_flag && (l = my_ismbchar( drizzle->charset, name , end ) ) )
    {
      while (l--)
        *to++ = *name++;
      name--;
      continue;
    }
#endif
    if (*name == '\\' && name[1])
      name++;
    *to++= *name;
  }
  *to=0;
}

int cli_unbuffered_fetch(DRIZZLE *drizzle, char **row)
{
  if (packet_error == cli_safe_read(drizzle))
    return 1;

  *row= ((drizzle->net.read_pos[0] == DRIZZLE_PROTOCOL_NO_MORE_DATA) ? NULL :
   (char*) (drizzle->net.read_pos+1));
  return 0;
}

/********************************************************************
 Transactional APIs
*********************************************************************/

/*
  Commit the current transaction
*/

bool STDCALL drizzle_commit(DRIZZLE *drizzle)
{
  return((bool) drizzle_real_query(drizzle, "commit", 6));
}

/*
  Rollback the current transaction
*/

bool STDCALL drizzle_rollback(DRIZZLE *drizzle)
{
  return((bool) drizzle_real_query(drizzle, "rollback", 8));
}


/*
  Set autocommit to either true or false
*/

bool STDCALL drizzle_autocommit(DRIZZLE *drizzle, bool auto_mode)
{
  return((bool) drizzle_real_query(drizzle, auto_mode ?
                                         "set autocommit=1":"set autocommit=0",
                                         16));
}


/********************************************************************
 Multi query execution + SPs APIs
*********************************************************************/

/*
  Returns true/false to indicate whether any more query results exist
  to be read using drizzle_next_result()
*/

bool STDCALL drizzle_more_results(const DRIZZLE *drizzle)
{
  return (drizzle->server_status & SERVER_MORE_RESULTS_EXISTS) ? true:false;
}


/*
  Reads and returns the next query results
*/
int STDCALL drizzle_next_result(DRIZZLE *drizzle)
{
  if (drizzle->status != DRIZZLE_STATUS_READY)
  {
    set_drizzle_error(drizzle, CR_COMMANDS_OUT_OF_SYNC, unknown_sqlstate);
    return(1);
  }

  net_clear_error(&drizzle->net);
  drizzle->affected_rows= ~(uint64_t) 0;

  if (drizzle->server_status & SERVER_MORE_RESULTS_EXISTS)
    return((*drizzle->methods->next_result)(drizzle));

  return(-1);        /* No more results */
}


DRIZZLE_RES * STDCALL drizzle_use_result(DRIZZLE *drizzle)
{
  return (*drizzle->methods->use_result)(drizzle);
}

bool STDCALL drizzle_read_query_result(DRIZZLE *drizzle)
{
  return (*drizzle->methods->read_query_result)(drizzle);
}

