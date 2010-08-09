/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include "config.h"

#include "plugin/session_dictionary/dictionary.h"

#include <netdb.h>

#include "drizzled/pthread_globals.h"
#include "drizzled/plugin/client.h"
#include "drizzled/plugin/authorization.h"
#include "drizzled/internal/my_sys.h"
#include "drizzled/internal/thread_var.h"

#include <set>

using namespace std;
using namespace drizzled;

ProcesslistTool::ProcesslistTool() :
  plugin::TableFunction("DATA_DICTIONARY", "PROCESSLIST")
{
  add_field("ID", plugin::TableFunction::NUMBER, 0, false);
  add_field("USER", 16);
  add_field("HOST", NI_MAXHOST);
  add_field("DB");
  add_field("COMMAND", 16);
  add_field("TIME", plugin::TableFunction::NUMBER, 0, false);
  add_field("STATE");
  add_field("INFO", PROCESS_LIST_WIDTH);
}

ProcesslistTool::Generator::Generator(Field **arg) :
  plugin::TableFunction::Generator(arg)
{
  now= time(NULL);

  LOCK_thread_count.lock();
  it= getSessionList().begin();
}

ProcesslistTool::Generator::~Generator()
{
  LOCK_thread_count.unlock();
}

bool ProcesslistTool::Generator::populate()
{
  const char *val;


  while (it != getSessionList().end())
  {
    if ((*it)->isViewable())
    {
      break;
    }
    ++it;
  }

  if (it == getSessionList().end())
    return false;

  Session *tmp= *it;
  const SecurityContext *tmp_sctx= &tmp->getSecurityContext();


  /* ID */
  push((int64_t) tmp->thread_id);


  /* USER */
  if (not tmp_sctx->getUser().empty())
    push(tmp_sctx->getUser());
  else 
    push("unauthenticated user");

  /* HOST */
  push(tmp_sctx->getIp());

  /* DB */
  if (! tmp->db.empty())
  {
    push(tmp->db);
  }
  else
  {
    push();
  }

  /* COMMAND */
  if ((val= const_cast<char *>(tmp->killed == Session::KILL_CONNECTION ? "Killed" : 0)))
  {
    push(val);
  }
  else
  {
    push(command_name[tmp->command].str, command_name[tmp->command].length);
  }

  /* DRIZZLE_TIME */
  push(static_cast<uint64_t>(tmp->start_time ?  now - tmp->start_time : 0));

  /* STATE */
  val= (char*) (tmp->client->isWriting() ?
                "Writing to net" :
                tmp->client->isReading() ?
                (tmp->command == COM_SLEEP ?
                 NULL : "Reading from net") :
                tmp->get_proc_info() ? tmp->get_proc_info() :
                tmp->mysys_var &&
                tmp->mysys_var->current_cond ?
                "Waiting on cond" : NULL);
  val ? push(val) : push();

  /* INFO */
  size_t length= strlen(tmp->process_list_info);
  length ?  push(tmp->process_list_info, length) : push();

  it++;

  return true;
}
