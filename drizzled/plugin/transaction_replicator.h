/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008-2009 Sun Microsystems
 *
 *  Authors:
 *
 *    Jay Pipes <joinfu@sun.com>
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

#ifndef DRIZZLED_PLUGIN_TRANSACTION_REPLICATOR_H
#define DRIZZLED_PLUGIN_TRANSACTION_REPLICATOR_H

#include "drizzled/atomics.h"

/**
 * @file Defines the API for a TransactionReplicator.  
 *
 * All a replicator does is replicate/reproduce
 * events, optionally transforming them before sending them off to a TransactionApplier.
 *
 * An applier is responsible for applying events, not a replicator...
 */


namespace drizzled
{
namespace message
{ 
  class Transaction;
  class Statement;
}

namespace plugin
{

class TransactionApplier;

/**
 * Class which replicates Transaction messages
 */
class TransactionReplicator : public Plugin
{
  TransactionReplicator();
  TransactionReplicator(const TransactionReplicator &);
  TransactionReplicator& operator=(const TransactionReplicator &);
  atomic<bool> is_enabled;
public:
  explicit TransactionReplicator(std::string name_arg)
    : Plugin(name_arg)
  {
    is_enabled= true;
  }
  virtual ~TransactionReplicator() {}

  /**
   * Replicate a Transaction message to a TransactionApplier.
   *
   * @note
   *
   * It is important to note that memory allocation for the 
   * supplied pointer is not guaranteed after the completion 
   * of this function -- meaning the caller can dispose of the
   * supplied message.  Therefore, replicators and appliers 
   * implementing an asynchronous replication system must copy
   * the supplied message to their own controlled memory storage
   * area.
   *
   * @param Pointer to the applier of the command message
   * @param Transaction message to be replicated
   */
  virtual void replicate(TransactionApplier *in_applier, 
                         message::Transaction &to_replicate)= 0;
  static bool addPlugin(TransactionReplicator *replicator);
  static void removePlugin(TransactionReplicator *replicator);

  virtual bool isEnabled() const
  {
    return is_enabled;
  }

  virtual void enable()
  {
    is_enabled= true;
  }

  virtual void disable()
  {
    is_enabled= false;
  }
};

} /* namespace plugin */
} /* namespace drizzled */

#endif /* DRIZZLED_PLUGIN_TRANSACTION_REPLICATOR_H */
