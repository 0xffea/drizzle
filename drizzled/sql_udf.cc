/* Copyright (C) 2000 MySQL AB

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

/* This implements 'user defined functions' */
#include <drizzled/server_includes.h>
#include <drizzled/gettext.h>
#include <mysys/hash.h>
#include <drizzled/sql_udf.h>

#include <map>
#include <string>

using namespace std;

static bool udf_startup= false; /* We do not lock because startup is single threaded */
static MEM_ROOT mem;
static map<string, udf_func *> udf_map;

extern "C" unsigned char* get_hash_key(const unsigned char *buff, size_t *length,
                               bool )
{
  udf_func *udf= (udf_func*) buff;
  *length= (uint) udf->name.length;
  return (unsigned char*) udf->name.str;
}


void udf_init()
{
  init_sql_alloc(&mem, UDF_ALLOC_BLOCK_SIZE, 0);
}

/* called by mysqld.cc clean_up() */
void udf_free()
{
  free_root(&mem, MYF(0));
}

/* This is only called if using_udf_functions != 0 */
udf_func *find_udf(const char *name, uint32_t length)
{
  udf_func *udf= NULL;

  if (udf_startup == false)
    return NULL;

  string find_str(name, length);
  transform(find_str.begin(), find_str.end(),
            find_str.begin(), ::tolower);

  map<string, udf_func *>::iterator find_iter;
  find_iter=  udf_map.find(find_str);
  if (find_iter != udf_map.end())
    udf= (*find_iter).second;

  return (udf);
}

static bool add_udf(udf_func *udf)
{
  string add_str(udf->name.str, udf->name.length);
  transform(add_str.begin(), add_str.end(),
            add_str.begin(), ::tolower);

  udf_map[add_str]= udf;

  using_udf_functions= 1;

  return true;
}

int initialize_udf(st_plugin_int *plugin)
{
  udf_func *f;

  if (udf_startup == false)
  {
    udf_init();
    udf_startup= true;
  }

  if (plugin->plugin->init)
  {
    int r;
    if ((r= plugin->plugin->init((void *)&f)))
    {
      errmsg_printf(ERRMSG_LVL_ERROR, "Plugin '%s' init function returned error %d.",
		    plugin->name.str, r);
      return r;
    }
  }
  else
    return 1;

  if (!add_udf(f))
    return 1;

  plugin->state= PLUGIN_IS_READY;

  return 0;

}

int finalize_udf(st_plugin_int *plugin)
{
  udf_func *udff = (udf_func *)plugin->data;

  /* TODO: Issue warning on failure */
  if (udff && plugin->plugin->deinit)
    (void)plugin->plugin->deinit(udff);

  if (udff)
    free(udff);

  return 0;
}

