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
#include <drizzled/sql_udf.h>
#include <drizzled/registry.h>
#include "drizzled/plugin/registry.h"

#include <string>

using namespace std;

static drizzled::Registry<Function_builder *> udf_registry;

Function_builder *find_udf(const char *name, uint32_t length)
{
  return udf_registry.find(name, length);
}

void add_udf(Function_builder *udf)
{
  udf_registry.add(udf);
}

void remove_udf(Function_builder *udf)
{
  udf_registry.remove(udf);
}


