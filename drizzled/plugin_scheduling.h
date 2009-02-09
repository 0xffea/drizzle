/*
 -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:

 *  Definitions required for Configuration Variables plugin

 *  Copyright (C) 2008 Mark Atwood
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

#ifndef DRIZZLED_PLUGIN_SCHEDULING_H
#define DRIZZLED_PLUGIN_SCHEDULING_H

typedef struct scheduling_st
{
  bool is_used;
  uint32_t max_threads;
  bool (*init_new_connection_thread)(void);
  bool (*add_connection)(Session *session);
  void (*post_kill_notification)(Session *session);
  bool (*end_thread)(Session *session, bool cache_thread);
} scheduling_st;

#endif /* DRIZZLED_PLUGIN_SCHEDULING_H */
