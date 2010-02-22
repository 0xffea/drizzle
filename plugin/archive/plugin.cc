/* Copyright (C) 2010 Brian Aker

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

#include "plugin/archive/archive_engine.h"

using namespace std;
using namespace drizzled;

static ArchiveEngine *archive_engine= NULL;

static bool archive_use_aio= false;

/* Used by the engie to determine the state of the archive AIO state */
bool archive_aio_state(void);

bool archive_aio_state(void)
{
  return archive_use_aio;
}

static int init(drizzled::plugin::Registry &registry)
{

  archive_engine= new ArchiveEngine();
  registry.add(archive_engine);

  return false;
}

/*
  Release the archive Cursor.

  SYNOPSIS
    archive_db_done()
    void

  RETURN
    false       OK
*/

extern pthread_mutex_t archive_mutex;

static int done(drizzled::plugin::Registry &registry)
{
  registry.remove(archive_engine);
  delete archive_engine;

  pthread_mutex_destroy(&archive_mutex);

  return 0;
}


static DRIZZLE_SYSVAR_BOOL(aio, archive_use_aio,
  PLUGIN_VAR_NOCMDOPT,
  "Whether or not to use asynchronous IO.",
  NULL, NULL, true);

static drizzle_sys_var* archive_system_variables[]= {
  DRIZZLE_SYSVAR(aio),
  NULL
};

DRIZZLE_DECLARE_PLUGIN
{
  DRIZZLE_VERSION_ID,
  "ARCHIVE",
  "3.5",
  "Brian Aker, MySQL AB",
  "Archive storage engine",
  PLUGIN_LICENSE_GPL,
  init, /* Plugin Init */
  done, /* Plugin Deinit */
  archive_system_variables,   /* system variables                */
  NULL                        /* config options                  */
}
DRIZZLE_DECLARE_PLUGIN_END;
