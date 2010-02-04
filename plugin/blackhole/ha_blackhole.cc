/* Copyright (C) 2005 MySQL AB

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

#include "config.h"
#include <drizzled/table.h>
#include <drizzled/error.h>
#include "drizzled/internal/my_pthread.h"

#include "ha_blackhole.h"

#include <fcntl.h>

#include <string>
#include <map>
#include <fstream>
#include <drizzled/message/table.pb.h>
#include "drizzled/internal/m_string.h"
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include "drizzled/global_charset_info.h"


using namespace std;
using namespace google;

#define BLACKHOLE_EXT ".blk"

static const char *ha_blackhole_exts[] = {
  NULL
};

class BlackholeEngine : public drizzled::plugin::StorageEngine
{
  typedef map<string, BlackholeShare*> BlackholeMap;
  BlackholeMap blackhole_open_tables;

public:
  BlackholeEngine(const string &name_arg)
   : drizzled::plugin::StorageEngine(name_arg, HTON_FILE_BASED |
                                     HTON_NULL_IN_KEY |
                                     HTON_CAN_INDEX_BLOBS |
                                     HTON_SKIP_STORE_LOCK |
                                     HTON_AUTO_PART_KEY |
                                     HTON_HAS_DATA_DICTIONARY),
    blackhole_open_tables()
  {
    table_definition_ext= BLACKHOLE_EXT;
  }

  virtual Cursor *create(TableShare &table,
                         drizzled::memory::Root *mem_root)
  {
    return new (mem_root) ha_blackhole(*this, table);
  }

  const char **bas_ext() const {
    return ha_blackhole_exts;
  }

  int doCreateTable(Session*,
                    const char *,
                    Table&,
                    drizzled::message::Table&);

  int doDropTable(Session&, const string table_name);

  BlackholeShare *findOpenTable(const string table_name);
  void addOpenTable(const string &table_name, BlackholeShare *);
  void deleteOpenTable(const string &table_name);

  int doGetTableDefinition(Session& session,
                           const char* path,
                           const char *db,
                           const char *table_name,
                           const bool is_tmp,
                           drizzled::message::Table *table_proto);

  void doGetTableNames(drizzled::CachedDirectory &directory,
                       string&, set<string>& set_of_names)
  {
    drizzled::CachedDirectory::Entries entries= directory.getEntries();

    for (drizzled::CachedDirectory::Entries::iterator entry_iter= entries.begin();
         entry_iter != entries.end(); ++entry_iter)
    {
      drizzled::CachedDirectory::Entry *entry= *entry_iter;
      const string *filename= &entry->filename;

      assert(filename->size());

      const char *ext= strchr(filename->c_str(), '.');

      if (ext == NULL || my_strcasecmp(system_charset_info, ext, BLACKHOLE_EXT) ||
         (filename->compare(0, strlen(TMP_FILE_PREFIX), TMP_FILE_PREFIX) == 0))
      {  }
      else
      {
        char uname[NAME_LEN + 1];
        uint32_t file_name_len;

        file_name_len= filename_to_tablename(filename->c_str(), uname, sizeof(uname));
        // TODO: Remove need for memory copy here
        uname[file_name_len - sizeof(BLACKHOLE_EXT) + 1]= '\0'; // Subtract ending, place NULL
        set_of_names.insert(uname);
      }
    }
  }

  /* The following defines can be increased if necessary */
  uint32_t max_supported_keys()          const { return BLACKHOLE_MAX_KEY; }
  uint32_t max_supported_key_length()    const { return BLACKHOLE_MAX_KEY_LENGTH; }
  uint32_t max_supported_key_part_length() const { return BLACKHOLE_MAX_KEY_LENGTH; }

  uint32_t index_flags(enum  ha_key_alg) const
  {
    return (HA_READ_NEXT |
            HA_READ_PREV |
            HA_READ_RANGE |
            HA_READ_ORDER |
            HA_KEYREAD_ONLY);
  }

};


BlackholeShare *BlackholeEngine::findOpenTable(const string table_name)
{
  BlackholeMap::iterator find_iter=
    blackhole_open_tables.find(table_name);

  if (find_iter != blackhole_open_tables.end())
    return (*find_iter).second;
  else
    return NULL;
}

void BlackholeEngine::addOpenTable(const string &table_name, BlackholeShare *share)
{
  blackhole_open_tables[table_name]= share;
}

void BlackholeEngine::deleteOpenTable(const string &table_name)
{
  blackhole_open_tables.erase(table_name);
}


/* Static declarations for shared structures */

static pthread_mutex_t blackhole_mutex;


/*****************************************************************************
** BLACKHOLE tables
*****************************************************************************/

ha_blackhole::ha_blackhole(drizzled::plugin::StorageEngine &engine_arg,
                           TableShare &table_arg)
  :Cursor(engine_arg, table_arg), share(NULL)
{ }

int ha_blackhole::open(const char *name, int, uint32_t)
{
  if (!(share= get_share(name)))
    return(HA_ERR_OUT_OF_MEM);

  thr_lock_data_init(&share->lock, &lock, NULL);
  return(0);
}

int ha_blackhole::close(void)
{
  free_share();
  return 0;
}

int BlackholeEngine::doCreateTable(Session*, const char *path,
                                   Table&,
                                   drizzled::message::Table& proto)
{
  string serialized_proto;
  string new_path;

  new_path= path;
  new_path+= BLACKHOLE_EXT;
  fstream output(new_path.c_str(), ios::out | ios::binary);


  if (! output)
    return 1;

  if (! proto.SerializeToOstream(&output))
  {
    output.close();
    unlink(new_path.c_str());
    return 1;
  }

  return 0;
}


int BlackholeEngine::doDropTable(Session&, const string path)
{
  string new_path(path);

  new_path+= BLACKHOLE_EXT;

  int error= unlink(new_path.c_str());

  if (error != 0)
  {
    error= errno= errno;
  }

  return error;
}


int BlackholeEngine::doGetTableDefinition(Session&,
                                          const char* path,
                                          const char *,
                                          const char *,
                                          const bool,
                                          drizzled::message::Table *table_proto)
{
  string new_path;

  new_path= path;
  new_path+= BLACKHOLE_EXT;

  int fd= open(new_path.c_str(), O_RDONLY);

  if (fd == -1)
  {
    return errno;
  }

  google::protobuf::io::ZeroCopyInputStream* input=
    new google::protobuf::io::FileInputStream(fd);

  if (! input)
    return HA_ERR_CRASHED_ON_USAGE;

  if (table_proto && ! table_proto->ParseFromZeroCopyStream(input))
  {
    close(fd);
    delete input;
    if (! table_proto->IsInitialized())
    {
      my_error(ER_CORRUPT_TABLE_DEFINITION, MYF(0),
               table_proto->InitializationErrorString().c_str());
      return ER_CORRUPT_TABLE_DEFINITION;
    }

    return HA_ERR_CRASHED_ON_USAGE;
  }

  delete input;
  return EEXIST;
}

const char *ha_blackhole::index_type(uint32_t)
{
  return("BTREE");
}

int ha_blackhole::write_row(unsigned char *)
{
  return(table->next_number_field ? update_auto_increment() : 0);
}

int ha_blackhole::rnd_init(bool)
{
  return(0);
}


int ha_blackhole::rnd_next(unsigned char *)
{
  return(HA_ERR_END_OF_FILE);
}


int ha_blackhole::rnd_pos(unsigned char *, unsigned char *)
{
  assert(0);
  return(0);
}


void ha_blackhole::position(const unsigned char *)
{
  assert(0);
  return;
}


int ha_blackhole::info(uint32_t flag)
{
  memset(&stats, 0, sizeof(stats));
  if (flag & HA_STATUS_AUTO)
    stats.auto_increment_value= 1;
  return(0);
}


int ha_blackhole::index_read_map(unsigned char *, const unsigned char *,
                                 key_part_map, enum ha_rkey_function)
{
  return(HA_ERR_END_OF_FILE);
}


int ha_blackhole::index_read_idx_map(unsigned char *, uint32_t, const unsigned char *,
                                     key_part_map, enum ha_rkey_function)
{
  return(HA_ERR_END_OF_FILE);
}


int ha_blackhole::index_read_last_map(unsigned char *, const unsigned char *, key_part_map)
{
  return(HA_ERR_END_OF_FILE);
}


int ha_blackhole::index_next(unsigned char *)
{
  return(HA_ERR_END_OF_FILE);
}


int ha_blackhole::index_prev(unsigned char *)
{
  return(HA_ERR_END_OF_FILE);
}


int ha_blackhole::index_first(unsigned char *)
{
  return(HA_ERR_END_OF_FILE);
}


int ha_blackhole::index_last(unsigned char *)
{
  return(HA_ERR_END_OF_FILE);
}


BlackholeShare *ha_blackhole::get_share(const char *table_name)
{
  pthread_mutex_lock(&blackhole_mutex);

  BlackholeEngine *a_engine= static_cast<BlackholeEngine *>(engine);
  share= a_engine->findOpenTable(table_name);

  if (share == NULL)
  {
    share= new (nothrow) BlackholeShare(table_name);
    if (share == NULL)
    {
      pthread_mutex_unlock(&blackhole_mutex);      
      return NULL;
    }

    a_engine->addOpenTable(share->table_name, share);
  }
  share->use_count++;
  pthread_mutex_unlock(&blackhole_mutex);
  return share;

}

void ha_blackhole::free_share()
{
  pthread_mutex_lock(&blackhole_mutex);
  if (!--share->use_count)
  {
    BlackholeEngine *a_engine= static_cast<BlackholeEngine *>(engine);
    a_engine->deleteOpenTable(share->table_name);
    delete share;
  }
  pthread_mutex_unlock(&blackhole_mutex);
}

BlackholeShare::BlackholeShare(const string table_name_arg)
  : use_count(0), table_name(table_name_arg)
{
  thr_lock_init(&lock);
}

BlackholeShare::~BlackholeShare()
{
  thr_lock_delete(&lock);
}


static drizzled::plugin::StorageEngine *blackhole_engine= NULL;

static int blackhole_init(drizzled::plugin::Registry &registry)
{

  blackhole_engine= new BlackholeEngine("BLACKHOLE");
  registry.add(blackhole_engine);
  
  pthread_mutex_init(&blackhole_mutex, MY_MUTEX_INIT_FAST);

  return 0;
}

static int blackhole_fini(drizzled::plugin::Registry &registry)
{
  registry.remove(blackhole_engine);
  delete blackhole_engine;

  pthread_mutex_destroy(&blackhole_mutex);

  return 0;
}

DRIZZLE_DECLARE_PLUGIN
{
  DRIZZLE_VERSION_ID,
  "BLACKHOLE",
  "1.0",
  "MySQL AB",
  "/dev/null storage engine (anything you write to it disappears)",
  PLUGIN_LICENSE_GPL,
  blackhole_init,     /* Plugin Init */
  blackhole_fini,     /* Plugin Deinit */
  NULL,               /* status variables */
  NULL,               /* system variables */
  NULL                /* config options   */
}
DRIZZLE_DECLARE_PLUGIN_END;
