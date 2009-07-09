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

#ifndef DRIZZLED_QCACHE_H
#define DRIZZLED_QCACHE_H

#include <drizzled/plugin/qcache.h>

void add_query_cache(QueryCache *handler);
void remove_query_cache(QueryCache *handler);

namespace drizzled {
namespace query_cache {
/* These are the functions called by the rest of the Drizzle server */
bool try_fetch_and_send(Session *session, bool transactional);
bool set(Session *session, bool transactional);
bool invalidate_table(Session *session, bool transactional);
bool invalidate_db(Session *session, const char *db_name,
                   bool transactional);
bool flush(Session *session);
}
}

#endif /* DRIZZLED_QCACHE_H */
