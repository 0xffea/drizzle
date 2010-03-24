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
 *
 */

#ifndef PLUGIN_LOGGING_STATS_SCOREBOARD_H
#define PLUGIN_LOGGING_STATS_SCOREBOARD_H

#include "scoreboard_slot.h"
#include <drizzled/session.h>

#include <vector>

class Scoreboard
{
public:
  Scoreboard(uint32_t in_number_sessions, uint32_t in_number_buckets);

  ~Scoreboard();

  ScoreboardSlot* findScoreboardSlotToLog(drizzled::Session *session);

  ScoreboardSlot* findScoreboardSlotToReset(drizzled::Session *session);

  uint32_t getNumberBuckets()
  {
    return number_buckets;
  }

  std::vector<pthread_rwlock_t* >* getVectorOfScoreboardLocks()
  {
    return vector_of_scoreboard_locks;
  }

  std::vector<std::vector<ScoreboardSlot* >* >* getVectorOfScoreboardVectors()
  {
    return vector_of_scoreboard_vectors; 
  }

private:
  static const int32_t UNINITIALIZED= -1;
  uint32_t number_sessions;
  uint32_t number_buckets;

  std::vector<std::vector<ScoreboardSlot* >* > *vector_of_scoreboard_vectors;
  std::vector<pthread_rwlock_t* > *vector_of_scoreboard_locks;
};

#endif /* PLUGIN_LOGGING_STATS_SCOREBOARD_H */
