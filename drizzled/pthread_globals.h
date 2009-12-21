/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
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

#ifndef DRIZZLED_PTHREAD_GLOBALS_H
#define DRIZZLED_PTHREAD_GLOBALS_H

#include <pthread.h>

extern pthread_mutex_t LOCK_create_db;
extern pthread_mutex_t LOCK_open;
extern pthread_mutex_t LOCK_thread_count;
extern pthread_mutex_t LOCK_status;
extern pthread_mutex_t LOCK_global_read_lock;
extern pthread_mutex_t LOCK_global_system_variables;

extern pthread_rwlock_t LOCK_system_variables_hash;
extern pthread_cond_t COND_refresh;
extern pthread_cond_t COND_thread_count;
extern pthread_cond_t COND_global_read_lock;
extern pthread_attr_t connection_attrib;
extern pthread_t signal_thread;

#endif /* DRIZZLED_PTHREAD_GLOBALS_H */
