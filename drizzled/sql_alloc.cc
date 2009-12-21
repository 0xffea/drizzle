/* Copyright (C) 2000-2001, 2003-2004 MySQL AB

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


/* Mallocs for used in threads */

#include <drizzled/server_includes.h>

#include "drizzled/sql_alloc.h"
#include "drizzled/current_session.h"
#include "drizzled/error.h"

extern "C" void sql_alloc_error_handler(void);

extern "C" void sql_alloc_error_handler(void)
{
  errmsg_printf(ERRMSG_LVL_ERROR, "%s",ER(ER_OUT_OF_RESOURCES));
}

void init_sql_alloc(MEM_ROOT *mem_root, size_t block_size, size_t pre_alloc)
{
  init_alloc_root(mem_root, block_size, pre_alloc);
  mem_root->error_handler= sql_alloc_error_handler;
}


void *sql_alloc(size_t Size)
{
  MEM_ROOT *root= current_mem_root();
  return alloc_root(root,Size);
}


void *sql_calloc(size_t size)
{
  void *ptr;
  if ((ptr=sql_alloc(size)))
    memset(ptr, 0, size);
  return ptr;
}


char *sql_strdup(const char *str)
{
  size_t len= strlen(str)+1;
  char *pos;
  if ((pos= (char*) sql_alloc(len)))
    memcpy(pos,str,len);
  return pos;
}


char *sql_strmake(const char *str, size_t len)
{
  char *pos;
  if ((pos= (char*) sql_alloc(len+1)))
  {
    memcpy(pos,str,len);
    pos[len]=0;
  }
  return pos;
}


void* sql_memdup(const void *ptr, size_t len)
{
  void *pos;
  if ((pos= sql_alloc(len)))
    memcpy(pos,ptr,len);
  return pos;
}

