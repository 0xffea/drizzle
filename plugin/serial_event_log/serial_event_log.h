/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008-2009 Sun Microsystems
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

/**
 * @file
 *
 * Defines the API of the default serial event log.
 *
 * @see drizzled/plugin/replicator.h
 * @see drizzled/plugin/applier.h
 *
 * @details
 *
 * The SerialEventLog applies events it receives from the TransactionServices
 * server component to a simple log file on disk.
 * 
 * Events are received in no guaranteed order and the serial event log
 * is in charge of writing these events to the log as they are received.
 */

#ifndef DRIZZLE_PLUGIN_SERIAL_EVENT_LOG_H
#define DRIZZLE_PLUGIN_SERIAL_EVENT_LOG_H

#include <drizzled/server_includes.h>
#include <drizzled/atomics.h>
#include <drizzled/plugin/replicator.h>
#include <drizzled/plugin/applier.h>

#include <vector>
#include <string>

class SerialEventLog: public drizzled::plugin::Applier 
{
public:
  enum status
  {
    CRASHED= 0,
    OFFLINE, /* Default state, uninited. */
    ONLINE,
    WRITING
  };
private:
  int log_file; /**< Handle for our log file */
  enum status state; /**< The state the log is in */
  drizzled::atomic<bool> is_enabled; /**< Internal toggle. Atomic to support online toggling of serial event log... */
  drizzled::atomic<bool> is_active; /**< Internal toggle. If true, log was initialized properly... */
  const char *log_file_path; /**< Full path to the log file */
  drizzled::atomic<off_t> log_offset; /**< Offset in log file where log will write next command */
public:
  SerialEventLog(const char *in_log_file_path);

  /** Destructor */
  ~SerialEventLog();

  /**
   * Applies a Command to the serial log
   *
   * @note
   *
   * It is important to note that memory allocation for the 
   * supplied pointer is not guaranteed after the completion 
   * of this function -- meaning the caller can dispose of the
   * supplied message.  Therefore, appliers which are
   * implementing an asynchronous replication system must copy
   * the supplied message to their own controlled memory storage
   * area.
   *
   * @param Command message to be replicated
   */
  void apply(drizzled::message::Command *to_apply);
  
  /** 
   * Returns whether the serial event log is active.
   */
  bool isActive();

  /**
   * Disables the plugin.
   * Disabled just means that the user has done an online set @serial_event_log_enable= false
   */
  inline void disable()
  {
    is_enabled= false;
  }

  /**
   * Enables the plugin.  Enabling is a bit different from isActive().
   * Enabled just means that the user has done an online set @serial_event_log_enable= true
   * or has manually started up the server with --serial-event-log-enable
   */
  inline void enable()
  {
    is_enabled= true;
  }

  /**
   * Returns the state that the log is in
   */
  inline enum status getState()
  {
    return state;
  }

  /**
   * Truncates the existing log file
   *
   * @note 
   *
   * This is only called currently during debugging and testing of the 
   * serial event log...when the @serial_event_log.truncate variable is 
   * set to anything other than false, this is called.
   */
  void truncate();
};

#endif /* DRIZZLE_PLUGIN_SERIAL_EVENT_LOG_H */
