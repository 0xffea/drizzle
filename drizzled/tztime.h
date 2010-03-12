/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
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

#ifndef DRIZZLED_TZTIME_H
#define DRIZZLED_TZTIME_H

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include "drizzled/memory/sql_alloc.h"

namespace drizzled
{

class String;
typedef struct st_drizzle_time DRIZZLE_TIME;

/**
  This class represents abstract time zone and provides
  basic interface for DRIZZLE_TIME <-> time_t conversion.
  Actual time zones which are specified by DB, or via offset
  or use system functions are its descendants.
*/
class Time_zone: public memory::SqlAlloc
{
public:
  Time_zone() {}                              /* Remove gcc warning */
  /**
    Converts local time in broken down DRIZZLE_TIME representation to
    time_t (UTC seconds since Epoch) represenation.
    Returns 0 in case of error. Sets in_dst_time_gap to true if date provided
    falls into spring time-gap (or lefts it untouched otherwise).
  */
  virtual time_t TIME_to_gmt_sec(const DRIZZLE_TIME *t,
                                 bool *in_dst_time_gap) const = 0;
  /**
    Converts time in time_t representation to local time in
    broken down DRIZZLE_TIME representation.
  */
  virtual void   gmt_sec_to_TIME(DRIZZLE_TIME *tmp, time_t t) const = 0;
  /**
    Because of constness of String returned by get_name() time zone name
    have to be already zeroended to be able to use String::ptr() instead
    of c_ptr().
  */
  virtual const String * get_name() const = 0;

  /**
    We need this only for surpressing warnings, objects of this type are
    allocated on memory::Root and should not require destruction.
  */
  virtual ~Time_zone() {};
};

extern Time_zone * my_tz_SYSTEM;
extern Time_zone * my_tz_find(Session *session, const String *name);
extern bool     my_tz_init(Session *org_session, const char *default_tzname);
extern time_t   sec_since_epoch_TIME(DRIZZLE_TIME *t);

} /* namespace drizzled */

#endif /* DRIZZLED_TZTIME_H */
