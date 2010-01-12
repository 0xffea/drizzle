/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 MySQL
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

#ifndef MYSYS_DRIZZLE_TIME_H
#define MYSYS_DRIZZLE_TIME_H

/*
  Time declarations shared between the server and client API:
  you should not add anything to this header unless it's used
  (and hence should be visible) in mysql.h.
  If you're looking for a place to add new time-related declaration,
  it's most likely my_time.h. See also "C API Handling of Date
  and Time Values" chapter in documentation.
*/

enum enum_drizzle_timestamp_type
{
  DRIZZLE_TIMESTAMP_NONE= -2, DRIZZLE_TIMESTAMP_ERROR= -1,
  DRIZZLE_TIMESTAMP_DATE= 0, DRIZZLE_TIMESTAMP_DATETIME= 1, DRIZZLE_TIMESTAMP_TIME= 2
};


/*
  Structure which is used to represent datetime values inside Drizzle.

  We assume that values in this structure are normalized, i.e. year <= 9999,
  month <= 12, day <= 31, hour <= 23, hour <= 59, hour <= 59. Many functions
  in server such as my_system_gmt_sec() or make_time() family of functions
  rely on this (actually now usage of make_*() family relies on a bit weaker
  restriction). Also functions that produce DRIZZLE_TIME as result ensure this.
  There is one exception to this rule though if this structure holds time
  value (time_type == DRIZZLE_TIMESTAMP_TIME) days and hour member can hold
  bigger values.
*/
typedef struct st_drizzle_time
{
  unsigned int  year, month, day, hour, minute, second;
  unsigned long second_part;
  bool       neg;
  enum enum_drizzle_timestamp_type time_type;
} DRIZZLE_TIME;

#endif /* MYSYS_DRIZZLE_TIME_H */
