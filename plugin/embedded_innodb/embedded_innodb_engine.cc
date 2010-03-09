/*
  Copyright (C) 2010 Stewart Smith

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "config.h"
#include <drizzled/table.h>
#include <drizzled/error.h>
#include "drizzled/internal/my_pthread.h"

#include "embedded_innodb_engine.h"

#include <fcntl.h>

#include <string>
#include <map>
#include <fstream>
#include <drizzled/message/table.pb.h>
#include "drizzled/internal/m_string.h"

#include "drizzled/global_charset_info.h"

#include "libinnodb_version_func.h"

#include "embedded_innodb-1.0/innodb.h"

using namespace std;
using namespace google;
using namespace drizzled;

#define EMBEDDED_INNODB_EXT ".EID"

static const char *EmbeddedInnoDBCursor_exts[] = {
  NULL
};

class EmbeddedInnoDBEngine : public drizzled::plugin::StorageEngine
{
public:
  EmbeddedInnoDBEngine(const string &name_arg)
   : drizzled::plugin::StorageEngine(name_arg,
                                     HTON_NULL_IN_KEY |
                                     HTON_CAN_INDEX_BLOBS |
                                     HTON_SKIP_STORE_LOCK |
                                     HTON_AUTO_PART_KEY |
                                     HTON_HAS_DATA_DICTIONARY)
  {
    table_definition_ext= EMBEDDED_INNODB_EXT;
  }

  virtual Cursor *create(TableShare &table,
                         drizzled::memory::Root *mem_root)
  {
    return new (mem_root) EmbeddedInnoDBCursor(*this, table);
  }

  const char **bas_ext() const {
    return EmbeddedInnoDBCursor_exts;
  }

  int doCreateTable(Session*,
                    const char *,
                    Table&,
                    drizzled::message::Table&);

  int doDropTable(Session&, const string table_name);

  int doGetTableDefinition(Session& session,
                           const char* path,
                           const char *db,
                           const char *table_name,
                           const bool is_tmp,
                           drizzled::message::Table *table_proto);

  void doGetTableNames(drizzled::CachedDirectory &,
                       string&, set<string>& )
  {
  }

  /* The following defines can be increased if necessary */
  uint32_t max_supported_keys()          const { return 64; }
  uint32_t max_supported_key_length()    const { return 1000; }
  uint32_t max_supported_key_part_length() const { return 1000; }

  uint32_t index_flags(enum  ha_key_alg) const
  {
    return (HA_READ_NEXT |
            HA_READ_PREV |
            HA_READ_RANGE |
            HA_READ_ORDER |
            HA_KEYREAD_ONLY);
  }

};

EmbeddedInnoDBCursor::EmbeddedInnoDBCursor(drizzled::plugin::StorageEngine &engine_arg,
                           TableShare &table_arg)
  :Cursor(engine_arg, table_arg)
{ }

int EmbeddedInnoDBCursor::open(const char *, int, uint32_t)
{
  return(0);
}

int EmbeddedInnoDBCursor::close(void)
{
  return 0;
}

int EmbeddedInnoDBEngine::doCreateTable(Session*, const char *,
                                   Table&,
                                   drizzled::message::Table&)
{
  return EEXIST;
}


int EmbeddedInnoDBEngine::doDropTable(Session&, const string)
{
  return EPERM;
}

int EmbeddedInnoDBEngine::doGetTableDefinition(Session&,
                                          const char* ,
                                          const char *,
                                          const char *,
                                          const bool,
                                          drizzled::message::Table *)
{
  return ENOENT;
}

const char *EmbeddedInnoDBCursor::index_type(uint32_t)
{
  return("BTREE");
}

int EmbeddedInnoDBCursor::write_row(unsigned char *)
{
  return(table->next_number_field ? update_auto_increment() : 0);
}

int EmbeddedInnoDBCursor::rnd_init(bool)
{
  return(0);
}


int EmbeddedInnoDBCursor::rnd_next(unsigned char *)
{
  return(HA_ERR_END_OF_FILE);
}


int EmbeddedInnoDBCursor::rnd_pos(unsigned char *, unsigned char *)
{
  assert(0);
  return(0);
}


void EmbeddedInnoDBCursor::position(const unsigned char *)
{
  assert(0);
  return;
}


int EmbeddedInnoDBCursor::info(uint32_t flag)
{
  memset(&stats, 0, sizeof(stats));
  if (flag & HA_STATUS_AUTO)
    stats.auto_increment_value= 1;
  return(0);
}


int EmbeddedInnoDBCursor::index_read_map(unsigned char *, const unsigned char *,
                                 key_part_map, enum ha_rkey_function)
{
  return(HA_ERR_END_OF_FILE);
}


int EmbeddedInnoDBCursor::index_read_idx_map(unsigned char *, uint32_t, const unsigned char *,
                                     key_part_map, enum ha_rkey_function)
{
  return(HA_ERR_END_OF_FILE);
}


int EmbeddedInnoDBCursor::index_read_last_map(unsigned char *, const unsigned char *, key_part_map)
{
  return(HA_ERR_END_OF_FILE);
}


int EmbeddedInnoDBCursor::index_next(unsigned char *)
{
  return(HA_ERR_END_OF_FILE);
}


int EmbeddedInnoDBCursor::index_prev(unsigned char *)
{
  return(HA_ERR_END_OF_FILE);
}


int EmbeddedInnoDBCursor::index_first(unsigned char *)
{
  return(HA_ERR_END_OF_FILE);
}


int EmbeddedInnoDBCursor::index_last(unsigned char *)
{
  return(HA_ERR_END_OF_FILE);
}

static drizzled::plugin::StorageEngine *embedded_innodb_engine= NULL;

static int embedded_innodb_init(drizzled::plugin::Registry &registry)
{
  int err;

  ib_init();
  /* call ib_cfg_*() */

  err= ib_startup("barracuda");

  if (err != DB_SUCCESS)
  {
    fprintf(stderr, "Error starting Embedded InnoDB %d\n", err);
    return -1;
  }

  embedded_innodb_engine= new EmbeddedInnoDBEngine("InnoDB");
  registry.add(embedded_innodb_engine);

  libinnodb_version_func_initialize(registry);

  return 0;
}

static int embedded_innodb_fini(drizzled::plugin::Registry &registry)
{
  int err;

  registry.remove(embedded_innodb_engine);
  delete embedded_innodb_engine;

  libinnodb_version_func_finalize(registry);

  err= ib_shutdown(IB_SHUTDOWN_NORMAL);

  if (err != DB_SUCCESS)
  {
    fprintf(stderr,"Error %d shutting down Embedded InnoDB!", err);
    return err;
  }

  return 0;
}

static char* innodb_data_file_path= NULL;

static DRIZZLE_SYSVAR_STR(data_file_path, innodb_data_file_path,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "Placeholder to be compatible with InnoDB plugin.",
  NULL, NULL, NULL);
static DRIZZLE_SessionVAR_ULONG(lock_wait_timeout, PLUGIN_VAR_RQCMDARG,
  "Placeholder: to be compatible with InnoDB plugin.",
  NULL, NULL, 50, 1, 1024 * 1024 * 1024, 0);

static drizzle_sys_var* innobase_system_variables[]= {
  DRIZZLE_SYSVAR(data_file_path),
  DRIZZLE_SYSVAR(lock_wait_timeout),
  NULL
};

DRIZZLE_DECLARE_PLUGIN
{
  DRIZZLE_VERSION_ID,
  "INNODB",
  "1.0",
  "Stewart Smith",
  "Used to test rest of server with various table proto messages",
  PLUGIN_LICENSE_GPL,
  embedded_innodb_init,     /* Plugin Init */
  embedded_innodb_fini,     /* Plugin Deinit */
  innobase_system_variables, /* system variables */
  NULL                /* config options   */
}
DRIZZLE_DECLARE_PLUGIN_END;
