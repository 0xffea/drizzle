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
 *
 *
 *
 */

#include "config.h"
#include "logging_stats.h"
#include "stats_schema.h"
#include <drizzled/session.h>

using namespace drizzled;
using namespace plugin;
using namespace std;

static bool sysvar_logging_stats_enabled= false;

static uint32_t sysvar_logging_stats_scoreboard_size= 1500;

pthread_rwlock_t LOCK_scoreboard;

LoggingStats::LoggingStats(std::string name_arg) : drizzled::plugin::Logging(name_arg)
{
  scoreboard_size= sysvar_logging_stats_scoreboard_size;
  score_board_slots= new ScoreBoardSlot[scoreboard_size];
  for (uint32_t j=0; j < scoreboard_size; j++)
  { 
    UserCommands *user_commands= new UserCommands();
    ScoreBoardSlot *score_board_slot= &score_board_slots[j];
    score_board_slot->setUserCommands(user_commands); 
  } 
}

LoggingStats::~LoggingStats()
{
  delete[] score_board_slots;
}

bool LoggingStats::isBeingLogged(Session *session)
{
  enum_sql_command sql_command= session->lex->sql_command;

  switch(sql_command)
  {
    case SQLCOM_UPDATE:
    case SQLCOM_DELETE:
    case SQLCOM_INSERT:
    case SQLCOM_ROLLBACK:
    case SQLCOM_COMMIT:
    case SQLCOM_CREATE_TABLE:
    case SQLCOM_ALTER_TABLE:
    case SQLCOM_DROP_TABLE:
    case SQLCOM_SELECT:
      return true;
    default:
      return false;
  }
} 

void LoggingStats::updateScoreBoard(ScoreBoardSlot *score_board_slot,
                                    Session *session)
{
  enum_sql_command sql_command= session->lex->sql_command;

  UserCommands *user_commands= score_board_slot->getUserCommands();

  switch(sql_command)
  {
    case SQLCOM_UPDATE:
      user_commands->incrementUpdateCount();
      break;
    case SQLCOM_DELETE:
      user_commands->incrementDeleteCount();
      break;
    case SQLCOM_INSERT:
      user_commands->incrementInsertCount();
      break;
    case SQLCOM_ROLLBACK:
      user_commands->incrementRollbackCount();
      break;
    case SQLCOM_COMMIT:
      user_commands->incrementCommitCount();
      break;
    case SQLCOM_CREATE_TABLE:
      user_commands->incrementCreateCount();
      break;
    case SQLCOM_ALTER_TABLE:
      user_commands->incrementAlterCount();
      break;
    case SQLCOM_DROP_TABLE:
      user_commands->incrementDropCount();
      break;
    case SQLCOM_SELECT:
      user_commands->incrementSelectCount();
      break;
    default:
      return;
  }
}

bool LoggingStats::post(Session *session)
{
  if (! isEnabled())
  {
    return false;
  }

  /* exit early if we are not logging this type of command */
  if (isBeingLogged(session) == false)
  {
    return false;
  }

  /* Find a slot that is unused */

  pthread_rwlock_wrlock(&LOCK_scoreboard);
  ScoreBoardSlot *score_board_slot;
  int our_slot= -1; 

  for (uint32_t j=0; j < scoreboard_size; j++)
  {
    score_board_slot= &score_board_slots[j];

    if (score_board_slot->isInUse() == true)
    {
      /* Check if this session is the one using this slot */
      if (score_board_slot->getSessionId() == session->getSessionId())
      {
        our_slot= j;
        break; 
      } 
      else 
      {
        continue; 
      }
    }
    else 
    {
      /* save off the open slot */ 
      our_slot= j;
      continue;
    }
  }

  if (our_slot != -1)
  {
    score_board_slot->setInUse(true);
    score_board_slot->setSessionId(session->getSessionId());
    score_board_slot->setUser(session->getSecurityContext().getUser());
    score_board_slot->setIp(session->getSecurityContext().getIp());
    pthread_rwlock_unlock(&LOCK_scoreboard); 
  }
  else 
  {
    pthread_rwlock_unlock(&LOCK_scoreboard);
    /* there was no available slot for this session */
    return false;
  }

  updateScoreBoard(score_board_slot, session);

  return false;
}

bool LoggingStats::postEnd(Session *session)
{
  if (! isEnabled())
  {
    return false;
  }

  /* do not pull a lock when we write this is the only thread that
     can write to a particular sessions slot. */

  ScoreBoardSlot *score_board_slot;

  for (uint32_t j=0; j < scoreboard_size; j++)
  {
    score_board_slot= &score_board_slots[j];

    if (score_board_slot->getSessionId() == session->getSessionId())
    {
      score_board_slot->reset();
      break;
    }
  }
  return false;
}

/* Plugin initialization and system variables */

static LoggingStats *logging_stats= NULL;

static CommandsTool *commands_tool= NULL;

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
  commands_tool= new(std::nothrow)CommandsTool(logging_stats);

  if (! commands_tool)
  {
    return true;
  }

  return false;
}

static int init(drizzled::plugin::Registry &registry)
{
  logging_stats= new LoggingStats("logging_stats");

  if (initTable())
  {
    return 1;
  }

  registry.add(logging_stats);
  registry.add(commands_tool);

  if (sysvar_logging_stats_enabled)
  {
    logging_stats->enable();
  }

  return 0;
}

static int deinit(drizzled::plugin::Registry &registry)
{
  if (logging_stats)
  {
    registry.remove(commands_tool);
    registry.remove(logging_stats);

    delete commands_tool;
    delete logging_stats;
  }
  return 0;
}

static DRIZZLE_SYSVAR_BOOL(enable,
                           sysvar_logging_stats_enabled,
                           PLUGIN_VAR_NOCMDARG,
                           N_("Enable Logging Statistics Collection"),
                           NULL, /* check func */
                           enable, /* update func */
                           false /* default */);

static drizzle_sys_var* system_var[]= {
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
  deinit, /* Plugin Deinit    */
  system_var, /* system variables */
  NULL    /* config options   */
}
DRIZZLE_DECLARE_PLUGIN_END;
