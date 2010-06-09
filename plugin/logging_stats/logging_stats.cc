/*
 * Copyright (c) 2010, Joseph Daly <skinny.moey@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *   * Neither the name of Joseph Daly nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @details
 *
 * This plugin tracks session and global statistics, as well as user statistics. 
 * The commands are logged using the post() and postEnd() logging APIs. 
 * The statistics are stored in a Scoreboard where each active session owns a 
 * ScoreboardSlot during the sessions active lifetime. 
 * 
 * Scoreboard
 *
 * The scoreboard is a pre-allocated vector of vectors of ScoreboardSlots. It 
 * can be thought of as a vector of buckets where each bucket contains 
 * pre-allocated ScoreboardSlots. To determine which bucket gets used for 
 * recording statistics the modulus operator is used on the session_id. This 
 * will result in a bucket to search for a unused ScoreboardSlot. Once a 
 * ScoreboardSlot is found the index of the slot is stored in the Session 
 * for later use. 
 *
 * Locking  
 * 
 * Each vector in the Scoreboard has its own lock. This allows session 2 
 * to not have to wait for session 1 to locate a slot to use, as they
 * will be in different buckets.  A lock is taken to locate a open slot
 * in the scoreboard. Subsequent queries by the session will not take
 * a lock.  
 *
 * A read lock is taken on the scoreboard vector when the table is queried 
 * in the data_dictionary. The "show status" and "show global status" do
 * not take a read lock when the data_dictionary table is queried, the 
 * user is not displayed in these results so it is not necessary. 
 *
 * Atomics
 *
 * The cumulative statistics use atomics, for the index into the vector
 * marking the last index that is used by a user. New users will increment
 * the atomic and claim the slot for use.  
 * 
 * System Variables
 * 
 * logging_stats_scoreboard_size - the size of the scoreboard this corresponds
 *   to the maximum number of concurrent connections that can be tracked
 *
 * logging_stats_max_user_count - this is used for cumulative statistics it 
 *   represents the maximum users that can be tracked 
 * 
 * logging_stats_bucket_count - the number of buckets to have in the scoreboard
 *   this splits up locking across several buckets so the entire scoreboard is 
 *   not locked at a single point in time.
 * 
 * logging_stats_enabled - enable/disable plugin 
 * 
 * TODO 
 *
 * Allow expansion of Scoreboard and cumulative vector 
 * 
 */

#include "config.h"
#include "user_commands.h"
#include "status_vars.h"
#include "global_stats.h"
#include "logging_stats.h"
#include "status_tool.h"
#include "stats_schema.h"

#include <drizzled/session.h>

using namespace drizzled;
using namespace plugin;
using namespace std;

static bool sysvar_logging_stats_enabled= true;

static uint32_t sysvar_logging_stats_scoreboard_size= 1000;

static uint32_t sysvar_logging_stats_max_user_count= 500;

static uint32_t sysvar_logging_stats_bucket_count= 10;

LoggingStats::LoggingStats(string name_arg) : Logging(name_arg)
{
  current_scoreboard= new Scoreboard(sysvar_logging_stats_scoreboard_size, 
                                     sysvar_logging_stats_bucket_count);

  cumulative_stats= new CumulativeStats(sysvar_logging_stats_max_user_count); 
}

LoggingStats::~LoggingStats()
{
  delete current_scoreboard;
  delete cumulative_stats;
}

void LoggingStats::updateCurrentScoreboard(ScoreboardSlot *scoreboard_slot,
                                           Session *session)
{
  enum_sql_command sql_command= session->lex->sql_command;

  scoreboard_slot->getUserCommands()->logCommand(sql_command);

  /* If a flush occurred copy over values before setting new values */
  if (scoreboard_slot->getStatusVars()->hasBeenFlushed(session))
  {
    cumulative_stats->logGlobalStatusVars(scoreboard_slot);
  }
  scoreboard_slot->getStatusVars()->logStatusVar(session);
}

bool LoggingStats::post(Session *session)
{
  if (! isEnabled() || (session->getSessionId() == 0))
  {
    return false;
  }

  ScoreboardSlot *scoreboard_slot= current_scoreboard->findScoreboardSlotToLog(session);

  /* Its possible that the scoreboard is full with active sessions in which case 
     this could be null */
  if (scoreboard_slot)
  {
    updateCurrentScoreboard(scoreboard_slot, session);
  }
  return false;
}

bool LoggingStats::postEnd(Session *session)
{
  if (! isEnabled() || (session->getSessionId() == 0))
  {
    return false;
  }

  ScoreboardSlot *scoreboard_slot= current_scoreboard->findAndResetScoreboardSlot(session);

  if (scoreboard_slot)
  {
    cumulative_stats->logUserStats(scoreboard_slot);
    cumulative_stats->logGlobalStats(scoreboard_slot);
    cumulative_stats->logGlobalStatusVars(scoreboard_slot);
    delete scoreboard_slot;
  }

  return false;
}

/* Plugin initialization and system variables */

static LoggingStats *logging_stats= NULL;

static CurrentCommandsTool *current_commands_tool= NULL;

static CumulativeCommandsTool *cumulative_commands_tool= NULL;

static GlobalStatementsTool *global_statements_tool= NULL;

static SessionStatementsTool *session_statements_tool= NULL;

static StatusTool *global_status_tool= NULL;

static StatusTool *session_status_tool= NULL;

static void enable(Session *,
                   drizzle_sys_var *,
                   void *var_ptr,
                   const void *save)
{
  if (logging_stats)
  {
    if (*(bool *)save != false)
    {
      logging_stats->enable();
      *(bool *) var_ptr= (bool) true;
    }
    else
    {
      logging_stats->disable();
      *(bool *) var_ptr= (bool) false;
    }
  }
}

static bool initTable()
{
  current_commands_tool= new(nothrow)CurrentCommandsTool(logging_stats);

  if (! current_commands_tool)
  {
    return true;
  }

  cumulative_commands_tool= new(nothrow)CumulativeCommandsTool(logging_stats);

  if (! cumulative_commands_tool)
  {
    return true;
  }

  global_statements_tool= new(nothrow)GlobalStatementsTool(logging_stats);

  if (! global_statements_tool)
  {
    return true;
  }

  session_statements_tool= new(nothrow)SessionStatementsTool(logging_stats);

  if (! session_statements_tool)
  {
    return true;
  }

  session_status_tool= new(nothrow)StatusTool(logging_stats, true);

  if (! session_status_tool)
  {
    return true;
  }

  global_status_tool= new(nothrow)StatusTool(logging_stats, false);

  if (! global_status_tool)
  {
    return true;
  }

  return false;
}

static int init(module::Context &context)
{
  logging_stats= new LoggingStats("logging_stats");

  if (initTable())
  {
    return 1;
  }

  context.add(logging_stats);
  context.add(current_commands_tool);
  context.add(cumulative_commands_tool);
  context.add(global_statements_tool);
  context.add(session_statements_tool);
  context.add(session_status_tool);
  context.add(global_status_tool);

  if (sysvar_logging_stats_enabled)
  {
    logging_stats->enable();
  }

  return 0;
}

static DRIZZLE_SYSVAR_UINT(max_user_count,
                           sysvar_logging_stats_max_user_count,
                           PLUGIN_VAR_RQCMDARG,
                           N_("Max number of users that will be logged"),
                           NULL, /* check func */
                           NULL, /* update func */
                           500, /* default */
                           100, /* minimum */
                           50000,
                           0);

static DRIZZLE_SYSVAR_UINT(bucket_count,
                           sysvar_logging_stats_bucket_count,
                           PLUGIN_VAR_RQCMDARG,
                           N_("Max number of vector buckets to construct for logging"),
                           NULL, /* check func */
                           NULL, /* update func */
                           10, /* default */
                           5, /* minimum */
                           500,
                           0);

static DRIZZLE_SYSVAR_UINT(scoreboard_size,
                           sysvar_logging_stats_scoreboard_size,
                           PLUGIN_VAR_RQCMDARG,
                           N_("Max number of concurrent sessions that will be logged"),
                           NULL, /* check func */
                           NULL, /* update func */
                           1000, /* default */
                           10, /* minimum */
                           50000, 
                           0);

static DRIZZLE_SYSVAR_BOOL(enable,
                           sysvar_logging_stats_enabled,
                           PLUGIN_VAR_NOCMDARG,
                           N_("Enable Logging Statistics Collection"),
                           NULL, /* check func */
                           enable, /* update func */
                           true /* default */);

static drizzle_sys_var* system_var[]= {
  DRIZZLE_SYSVAR(max_user_count),
  DRIZZLE_SYSVAR(bucket_count),
  DRIZZLE_SYSVAR(scoreboard_size),
  DRIZZLE_SYSVAR(enable),
  NULL
};

DRIZZLE_DECLARE_PLUGIN
{
  DRIZZLE_VERSION_ID,
  "logging_stats",
  "0.1",
  "Joseph Daly",
  N_("User Statistics as DATA_DICTIONARY tables"),
  PLUGIN_LICENSE_BSD,
  init,   /* Plugin Init      */
  system_var, /* system variables */
  NULL    /* config options   */
}
DRIZZLE_DECLARE_PLUGIN_END;
