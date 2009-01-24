/* Copyright (C) 2009 Sun Microsystems

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

/*
  session_scheduler keeps the link between Session and events.
  It's embedded in the Session class.
*/

#include <drizzled/session.h>
#include <drizzled/gettext.h>
#include <drizzled/errmsg_print.h>
#include <event.h>
/* API for connecting, logging in to a drizzled server */
#include <drizzled/connect.h>
#include "session_scheduler.h"

/* Prototype */
void libevent_io_callback(int Fd, short Operation, void *ctx);
bool libevent_should_close_connection(Session* session);
void libevent_session_add(Session* session);

session_scheduler::session_scheduler()
  : logged_in(false), io_event(NULL), thread_attached(false)
{
}


session_scheduler::~session_scheduler()
{
  delete io_event;
}


session_scheduler::session_scheduler(const session_scheduler&)
  : logged_in(false), io_event(NULL), thread_attached(false)
{}

void session_scheduler::operator=(const session_scheduler&)
{}

bool session_scheduler::init(Session *parent_session)
{
  io_event= new struct event;

  if (io_event == NULL)
  {
    errmsg_printf(ERRMSG_LVL_ERROR, _("Memory allocation error in session_scheduler::init\n"));
    return true;
  }
  memset(io_event, 0, sizeof(*io_event));

  event_set(io_event, net_get_sd(&(parent_session->net)), EV_READ,
            libevent_io_callback, (void*)parent_session);

  list.data= parent_session;

  return false;
}


/*
  Attach/associate the connection with the OS thread, for command processing.
*/

bool session_scheduler::thread_attach()
{
  assert(!thread_attached);
  Session* session = (Session*)list.data;
  if (libevent_should_close_connection(session) ||
      setup_connection_thread_globals(session))
  {
    return true;
  }
  my_errno= 0;
  session->mysys_var->abort= 0;
  thread_attached= true;

  return false;
}


/*
  Detach/disassociate the connection with the OS thread.
*/

void session_scheduler::thread_detach()
{
  if (thread_attached)
  {
    Session* session = (Session*)list.data;
    session->mysys_var= NULL;
    thread_attached= false;
  }
}