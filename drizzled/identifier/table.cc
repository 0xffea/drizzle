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

#include "config.h"

#include <assert.h>

#include "drizzled/identifier.h"
#include "drizzled/session.h"
#include "drizzled/internal/my_sys.h"
#include "drizzled/data_home.h"

#include "drizzled/table.h"

#include "drizzled/util/string.h"

#include <algorithm>
#include <sstream>
#include <cstdio>

#include <boost/thread.hpp>

using namespace std;

namespace drizzled
{

extern std::string drizzle_tmpdir;
extern pid_t current_pid;

static const char hexchars[]= "0123456789abcdef";

static bool tablename_to_filename(const char *from, char *to, size_t to_length);

/*
  Translate a cursor name to a table name (WL #1324).

  SYNOPSIS
    filename_to_tablename()
      from                      The cursor name
      to                OUT     The table name
      to_length                 The size of the table name buffer.

  RETURN
    Table name length.
*/
uint32_t TableIdentifier::filename_to_tablename(const char *from, char *to, uint32_t to_length)
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
  Creates path to a cursor: drizzle_tmpdir/#sql1234_12_1.ext

  SYNOPSIS
   build_tmptable_filename()
     session                    The thread handle.
     buff                       Where to write result
     bufflen                    buff size

  NOTES

    Uses current_pid, thread_id, and tmp_table counter to create
    a cursor name in drizzle_tmpdir.

  RETURN
    path length on success, 0 on failure
*/

#ifdef _GLIBCXX_HAVE_TLS 
__thread uint32_t counter= 0;

static uint32_t get_counter()
{
  return ++counter;
}

#else
boost::mutex counter_mutex;
static uint32_t counter= 1;

static uint32_t get_counter()
{
  boost::mutex::scoped_lock lock(counter_mutex);
  uint32_t x;
  x= ++counter;

  return x;
}

#endif

size_t TableIdentifier::build_tmptable_filename(std::string &buffer)
{
  size_t tmpdir_length;
  ostringstream post_tmpdir_str;

  buffer.append(drizzle_tmpdir);
  tmpdir_length= buffer.length();

  post_tmpdir_str << "/" << TMP_FILE_PREFIX << current_pid;
  post_tmpdir_str << pthread_self() << "-" << get_counter();

  buffer.append(post_tmpdir_str.str());

  transform(buffer.begin() + tmpdir_length, buffer.end(), buffer.begin() + tmpdir_length, ::tolower);

  return buffer.length();
}

size_t TableIdentifier::build_tmptable_filename(std::vector<char> &buffer)
{
  ostringstream post_tmpdir_str;

  post_tmpdir_str << drizzle_tmpdir << "/" << TMP_FILE_PREFIX << current_pid;
  post_tmpdir_str << pthread_self() << "-" << get_counter();

  buffer.resize(post_tmpdir_str.str().length() + 1);
  memcpy(&buffer[0], post_tmpdir_str.str().c_str(), post_tmpdir_str.str().size());
  buffer[post_tmpdir_str.str().size()]= 0;

  return buffer.size();
}


/*
  Creates path to a cursor: drizzle_data_dir/db/table.ext

  SYNOPSIS
   build_table_filename()
     buff                       Where to write result
                                This may be the same as table_name.
     bufflen                    buff size
     db                         Database name
     table_name                 Table name
     ext                        File extension.
     flags                      table_name is temporary, do not change.

  NOTES

    Uses database and table name, and extension to create
    a cursor name in drizzle_data_dir. Database and table
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

size_t TableIdentifier::build_table_filename(std::string &path, const char *db, const char *table_name, bool is_tmp)
{
  char dbbuff[FN_REFLEN];
  char tbbuff[FN_REFLEN];
  bool conversion_error= false;

  memset(tbbuff, 0, sizeof(tbbuff));
  if (is_tmp) // It a conversion tmp
  {
    strncpy(tbbuff, table_name, sizeof(tbbuff));
  }
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
  path.append(getDataHomeCatalog());
  ssize_t without_rootdir= path.length() - rootdir_len;

  /* Don't add FN_ROOTDIR if dirzzle_data_home already includes it */
  if (without_rootdir >= 0)
  {
    const char *tmp= path.c_str() + without_rootdir;

    if (memcmp(tmp, FN_ROOTDIR, rootdir_len) != 0)
      path.append(FN_ROOTDIR);
  }

  path.append(dbbuff);
  path.append(FN_ROOTDIR);
  path.append(tbbuff);

  return path.length();
}


/*
  Translate a table name to a cursor name (WL #1324).

  SYNOPSIS
    tablename_to_filename()
      from                      The table name
      to                OUT     The cursor name
      to_length                 The size of the cursor name buffer.

  RETURN
    true if errors happen. false on success.
*/
static bool tablename_to_filename(const char *from, char *to, size_t to_length)
{
  
  size_t length= 0;
  for (; *from  && length < to_length; length++, from++)
  {
    if ((*from >= '0' && *from <= '9') ||
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
      to[length]= tolower(*from);
      continue;
    }

    if ((*from >= 'A' && *from <= 'Z'))
    {
      to[length]= tolower(*from);
      continue;
    }
   
    if (length + 3 >= to_length)
      return true;

    /* We need to escape this char in a way that can be reversed */
    to[length++]= '@';
    to[length++]= hexchars[(*from >> 4) & 15];
    to[length]= hexchars[(*from) & 15];
  }

  if (internal::check_if_legal_tablename(to) &&
      length + 4 < to_length)
  {
    memcpy(to + length, "@@@", 4);
    length+= 3;
  }
  return false;
}

TableIdentifier::TableIdentifier(const drizzled::Table &table) :
  SchemaIdentifier(table.getShare()->getSchemaName()),
  type(table.getShare()->getTableType()),
  table_name(table.getShare()->getTableName())
{
  if (type == message::Table::TEMPORARY)
    path= table.getShare()->getPath();

  init();
}

void TableIdentifier::init()
{
  switch (type) {
  case message::Table::FUNCTION:
  case message::Table::STANDARD:
    assert(path.size() == 0);
    build_table_filename(path, getSchemaName().c_str(), table_name.c_str(), false);
    break;
  case message::Table::INTERNAL:
    assert(path.size() == 0);
    build_table_filename(path, getSchemaName().c_str(), table_name.c_str(), true);
    break;
  case message::Table::TEMPORARY:
    if (path.empty())
    {
      build_tmptable_filename(path);
    }
    break;
  }

  util::insensitive_hash hasher;
  hash_value= hasher(path);

  key.resize(getKeySize());
  size_t key_length= TableIdentifier::createKey(&key[0], *this);

  assert(key_length == getKeySize()); // If this is off, then we have a memory issue.
}


const std::string &TableIdentifier::getPath() const
{
  return path;
}

const std::string &TableIdentifier::getSQLPath()  // @todo this is just used for errors, we should find a way to optimize it
{
  if (sql_path.empty())
  {
    switch (type) {
    case message::Table::FUNCTION:
    case message::Table::STANDARD:
      sql_path.append(getSchemaName());
      sql_path.append(".");
      sql_path.append(table_name);
      break;
    case message::Table::INTERNAL:
      sql_path.append("temporary.");
      sql_path.append(table_name);
      break;
    case message::Table::TEMPORARY:
      sql_path.append(getSchemaName());
      sql_path.append(".#");
      sql_path.append(table_name);
      break;
    }
  }

  return sql_path;
}


void TableIdentifier::copyToTableMessage(message::Table &message) const
{
  message.set_name(table_name);
  message.set_schema(getSchemaName());
}

std::size_t hash_value(TableIdentifier const& b)
{
  return b.getHashValue();
}

} /* namespace drizzled */
