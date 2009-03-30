/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
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

#ifndef DRIZZLED_GLOBAL_QUERY_ID_H
#define DRIZZLED_GLOBAL_QUERY_ID_H

#include <pthread.h>
#include <drizzled/atomics.h>

class Query_id
{
public:
  static Query_id& get_query_id() {
    static Query_id the_id;
    return the_id;
  }
  ~Query_id();

  /* return current query_id value */
  query_id_t value() const;

  /* increment query_id and return it.  */
  query_id_t next();

private:
  drizzled::atomic<uint64_t> the_query_id;

  Query_id();
  Query_id(Query_id const&);
  Query_id& operator=(Query_id const&);
};

#endif
