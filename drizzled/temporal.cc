/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
 *
 *  Authors:
 *
 *  Jay Pipes <jay.pipes@sun.com>
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
 * Implementation of the server's temporal class API
 *
 * @todo
 *
 * Move to completed ValueObject API, which would remove the from_xxx() methods
 * and replace them with constructors which take other ValueObject instances as
 * their single parameter.
 */

#include "drizzled/global.h"

#include "mystrings/m_ctype.h"
#include "drizzled/my_decimal.h"
#include "drizzled/calendar.h"
#include "drizzled/temporal.h"
#ifdef NOTYETIMPLEMENTED
#include "drizzled/temporal_interval.h"
#endif
#include "drizzled/temporal_format.h"

#include <ostream>
#include <iomanip>
#include <vector>
#include <string.h>

/* time.h may already have been included in global.h, but we
   need to pick up the extra defs as well, after the global time.h */
#ifndef HAVE_DECL_TIMEGM
# include <gnulib/time.h>
#endif

extern std::vector<drizzled::TemporalFormat *> known_datetime_formats;
extern std::vector<drizzled::TemporalFormat *> known_date_formats;
extern std::vector<drizzled::TemporalFormat *> known_time_formats;

namespace drizzled 
{

Temporal::Temporal()
:
  _calendar(GREGORIAN)
, _years(0)
, _months(0)
, _days(0)
, _hours(0)
, _minutes(0)
, _seconds(0)
, _epoch_seconds(0)
, _useconds(0)
, _nseconds(0)
, _overflow(false)
{}

uint64_t Temporal::_cumulative_seconds_in_time() const
{
  return (uint64_t) ((_hours * INT64_C(3600)) 
      + (_minutes * INT64_C(60)) 
      + _seconds);
}

void Temporal::set_epoch_seconds()
{
  /* 
   * If the temporal is in the range of a timestamp, set 
   * the epoch_seconds member variable
   */
  if (in_unix_epoch_range(_years, _months, _days, _hours, _minutes, _seconds))
  {
    time_t result_time;
    struct tm broken_time;

    broken_time.tm_sec= _seconds;
    broken_time.tm_min= _minutes;
    broken_time.tm_hour= _hours;
    broken_time.tm_mday= _days; /* Drizzle format uses ordinal, standard tm does too! */
    broken_time.tm_mon= _months - 1; /* Drizzle format uses ordinal, standard tm does NOT! */
    broken_time.tm_year= _years - 1900; /* tm_year expects range of 70 - 38 */

    result_time= timegm(&broken_time);

    _epoch_seconds= result_time;
  }
}

bool Date::from_string(const char *from, size_t from_len)
{
  /* 
   * Loop through the known date formats and see if 
   * there is a match.
   */
  bool matched= false;
  TemporalFormat *current_format;
  std::vector<TemporalFormat *>::iterator current= known_date_formats.begin();

  while (current != known_date_formats.end())
  {
    current_format= *current;
    if (current_format->matches(from, from_len, this))
    {
      matched= true;
      break;
    }
    current++;
  }

  if (! matched)
    return false;

  set_epoch_seconds();
  return is_valid();
}

bool DateTime::from_string(const char *from, size_t from_len)
{
  /* 
   * Loop through the known datetime formats and see if 
   * there is a match.
   */
  bool matched= false;
  TemporalFormat *current_format;
  std::vector<TemporalFormat *>::iterator current= known_datetime_formats.begin();

  while (current != known_datetime_formats.end())
  {
    current_format= *current;
    if (current_format->matches(from, from_len, this))
    {
      matched= true;
      break;
    }
    current++;
  }

  if (! matched)
    return false;

  set_epoch_seconds();
  return is_valid();
}

/*
 * Comparison operators for Time against another Time
 * are easy.  We simply compare the cumulative time
 * value of each.
 */
bool Time::operator==(const Time& rhs)
{
  return (
          _hours == rhs._hours
       && _minutes == rhs._minutes
       && _seconds == rhs._seconds
       && _useconds == rhs._useconds
       && _nseconds == rhs._nseconds
      );
}
bool Time::operator!=(const Time& rhs)
{
  return ! (*this == rhs);
}
bool Time::operator<(const Time& rhs)
{
  return (_cumulative_seconds_in_time() < rhs._cumulative_seconds_in_time());
}
bool Time::operator<=(const Time& rhs)
{
  return (_cumulative_seconds_in_time() <= rhs._cumulative_seconds_in_time());
}
bool Time::operator>(const Time& rhs)
{
  return (_cumulative_seconds_in_time() > rhs._cumulative_seconds_in_time());
}
bool Time::operator>=(const Time& rhs)
{
  return (_cumulative_seconds_in_time() >= rhs._cumulative_seconds_in_time());
}

/** 
 * Subtracting one Time value from another can yield
 * a new Time instance.
 *
 * This operator is called in the following situation:
 *
 * @code
 * drizzled::Time lhs;
 * lhs.from_string("20:00:00");
 * drizzled::Time rhs;
 * rhs.from_string("19:00:00");
 *
 * drizzled::Time result= lhs - rhs;
 * @endcode
 *
 * @note
 *
 * Subtracting a larger time value from a smaller one
 * should throw an exception at some point.  The result
 * of such an operator should be a TemporalInterval, not
 * a Time instance, since a negative time is not possible.
 */
const Time Time::operator-(const Time& rhs)
{
  Time result;

  int64_t second_diff= _cumulative_seconds_in_time() - rhs._cumulative_seconds_in_time();
  result._hours= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_HOUR;
  second_diff%= DRIZZLE_SECONDS_IN_HOUR;
  result._minutes= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_MINUTE;
  second_diff%= DRIZZLE_SECONDS_IN_MINUTE;
  result._seconds= (uint32_t) second_diff;
  
  return result;
}
const Time Time::operator+(const Time& rhs)
{
  Time result;
  int64_t second_diff= _cumulative_seconds_in_time() + rhs._cumulative_seconds_in_time();
  result._hours= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_HOUR;
  second_diff%= DRIZZLE_SECONDS_IN_HOUR;
  result._minutes= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_MINUTE;
  second_diff%= DRIZZLE_SECONDS_IN_MINUTE;
  result._seconds= (uint32_t) second_diff;
  /** 
   * @TODO Once exceptions are supported, we should raise an error here if
   *       the result Time is not valid?
   */
  return result;
}

/*
 * Variation of + and - operator which returns a reference to the left-hand
 * side Time object and adds the right-hand side to itself.
 */
Time& Time::operator+=(const Time& rhs)
{
  int64_t second_diff= _cumulative_seconds_in_time() + rhs._cumulative_seconds_in_time();
  _hours= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_HOUR;
  second_diff%= DRIZZLE_SECONDS_IN_HOUR;
  _minutes= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_MINUTE;
  second_diff%= DRIZZLE_SECONDS_IN_MINUTE;
  _seconds= (uint32_t) second_diff;
  /** 
   * @TODO Once exceptions are supported, we should raise an error here if
   *       the result Time is not valid?
   */
  return *this;
}
Time& Time::operator-=(const Time& rhs)
{
  int64_t second_diff= _cumulative_seconds_in_time() - rhs._cumulative_seconds_in_time();
  _hours= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_HOUR;
  second_diff%= DRIZZLE_SECONDS_IN_HOUR;
  _minutes= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_MINUTE;
  second_diff%= DRIZZLE_SECONDS_IN_MINUTE;
  _seconds= (uint32_t) second_diff;
  /** 
   * @TODO Once exceptions are supported, we should raise an error here if
   *       the result Time is not valid?
   */
  return *this;
}

/*
 * Comparison operators for Date against another Date
 * are easy.  We simply compare the cumulative
 * value of each.
 */
bool Date::operator==(const Date& rhs)
{
  return (
          _years == rhs._years
       && _months == rhs._months
       && _days == rhs._days
      );
}
bool Date::operator!=(const Date& rhs)
{
  return ! (*this == rhs);
}
bool Date::operator<(const Date& rhs)
{
  int64_t days_left= julian_day_number_from_gregorian_date(_years, _months, _days);
  int64_t days_right= julian_day_number_from_gregorian_date(rhs._years, rhs._months, rhs._days);
  return (days_left < days_right);
}
bool Date::operator<=(const Date& rhs)
{
  int64_t days_left= julian_day_number_from_gregorian_date(_years, _months, _days);
  int64_t days_right= julian_day_number_from_gregorian_date(rhs._years, rhs._months, rhs._days);
  return (days_left <= days_right);
}
bool Date::operator>(const Date& rhs)
{
  return ! (*this <= rhs);
}
bool Date::operator>=(const Date& rhs)
{
  return ! (*this < rhs);
}

/*
 * Comparison operators for DateTime against another DateTime
 * are easy.  We simply compare the cumulative time
 * value of each.
 */
bool Date::operator==(const DateTime& rhs)
{
  return (
          _years == rhs._years
       && _months == rhs._months
       && _days == rhs._days
       && _hours == rhs._hours
       && _minutes == rhs._minutes
       && _seconds == rhs._seconds
       && _useconds == rhs._useconds
       && _nseconds == rhs._nseconds
      );
}
bool Date::operator!=(const DateTime& rhs)
{
  return ! (*this == rhs);
}
bool Date::operator<(const DateTime& rhs)
{
  int64_t days_left= julian_day_number_from_gregorian_date(_years, _months, _days);
  int64_t days_right= julian_day_number_from_gregorian_date(rhs._years, rhs._months, rhs._days);
  if (days_left < days_right)
    return true;
  else if (days_left > days_right)
    return false;
  /* Here if both dates are the same, so compare times */
  return (_cumulative_seconds_in_time() < rhs._cumulative_seconds_in_time());
}
bool Date::operator<=(const DateTime& rhs)
{
  int64_t days_left= julian_day_number_from_gregorian_date(_years, _months, _days);
  int64_t days_right= julian_day_number_from_gregorian_date(rhs._years, rhs._months, rhs._days);
  if (days_left < days_right)
    return true;
  else if (days_left > days_right)
    return false;
  /* Here if both dates are the same, so compare times */
  return (_cumulative_seconds_in_time() <= rhs._cumulative_seconds_in_time());
}
bool Date::operator>(const DateTime& rhs)
{
  return ! (*this <= rhs);
}
bool Date::operator>=(const DateTime& rhs)
{
  return ! (*this < rhs);
}

/** 
 * We can add or subtract a Time value to/from a DateTime value 
 * as well...it always produces a DateTime.
 */
const Date Date::operator-(const Time& rhs)
{
  DateTime result;

  /* 
   * First, we set the resulting DATE pieces equal to our 
   * left-hand side DateTime's DATE components. Then, deal with 
   * the time components.
   */
  result._years= _years;
  result._months= _months;
  result._days= _days;

  int64_t second_diff= _cumulative_seconds_in_time() - rhs._cumulative_seconds_in_time();

  /* 
   * The resulting diff might be negative.  If it is, that means that 
   * we have subtracting a larger time piece from the datetime, like so:
   *
   * x = DateTime("2007-06-09 09:30:00") - Time("16:30:00");
   *
   * In these cases, we need to subtract a day from the resulting
   * DateTime.
   */
  if (second_diff < 0)
    result._days--;

  result._hours= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_HOUR;
  second_diff%= DRIZZLE_SECONDS_IN_HOUR;
  result._minutes= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_MINUTE;
  second_diff%= DRIZZLE_SECONDS_IN_MINUTE;
  result._seconds= (uint32_t) second_diff;

  /* Handle the microsecond precision */
  int64_t microsecond_diff= _useconds - rhs._useconds;
  if (microsecond_diff < 0)
  {
    microsecond_diff= (-1 * microsecond_diff);
    result._seconds--;
  }
  result._useconds= (uint32_t) microsecond_diff;

  return result;
}
const Date Date::operator+(const Time& rhs)
{
  DateTime result;

  /* 
   * First, we set the resulting DATE pieces equal to our 
   * left-hand side DateTime's DATE components. Then, deal with 
   * the time components.
   */
  result._years= _years;
  result._months= _months;
  result._days= _days;

  int64_t second_diff= _cumulative_seconds_in_time() + rhs._cumulative_seconds_in_time();

  /* 
   * The resulting seconds might be more than a day.  If do, 
   * adjust our resulting days up 1.
   */
  if (second_diff >= DRIZZLE_SECONDS_IN_DAY)
  {
    result._days++;
    second_diff%= DRIZZLE_SECONDS_IN_DAY;
  }

  result._hours= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_HOUR;
  second_diff%= DRIZZLE_SECONDS_IN_HOUR;
  result._minutes= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_MINUTE;
  second_diff%= DRIZZLE_SECONDS_IN_MINUTE;
  result._seconds= (uint32_t) second_diff;

  /* Handle the microsecond precision */
  int64_t microsecond_diff= _useconds - rhs._useconds;
  if (microsecond_diff < 0)
  {
    microsecond_diff= (-1 * microsecond_diff);
    result._seconds--;
  }
  result._useconds= (uint32_t) microsecond_diff;

  return result;
}

/*
 * Variation of + and - operator which returns a reference to the left-hand
 * side DateTime object and adds the right-hand side Time to itself.
 */
Date& Date::operator+=(const Time& rhs)
{
  int64_t second_diff= _cumulative_seconds_in_time() + rhs._cumulative_seconds_in_time();
  /* 
   * The resulting seconds might be more than a day.  If do, 
   * adjust our resulting days up 1.
   */
  if (second_diff >= DRIZZLE_SECONDS_IN_DAY)
  {
    _days++;
    second_diff%= DRIZZLE_SECONDS_IN_DAY;
  }

  _hours= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_HOUR;
  second_diff%= DRIZZLE_SECONDS_IN_HOUR;
  _minutes= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_MINUTE;
  second_diff%= DRIZZLE_SECONDS_IN_MINUTE;
  _seconds= (uint32_t) second_diff;

  /* Handle the microsecond precision */
  int64_t microsecond_diff= _useconds - rhs._useconds;
  if (microsecond_diff < 0)
  {
    microsecond_diff= (-1 * microsecond_diff);
    _seconds--;
  }
  _useconds= (uint32_t) microsecond_diff;
  /** 
   * @TODO Once exceptions are supported, we should raise an error here if
   *       the result Time is not valid?
   */
  return *this;
}
Date& Date::operator-=(const Time& rhs)
{
  int64_t second_diff= _cumulative_seconds_in_time() - rhs._cumulative_seconds_in_time();

  /* 
   * The resulting diff might be negative.  If it is, that means that 
   * we have subtracting a larger time piece from the datetime, like so:
   *
   * x = DateTime("2007-06-09 09:30:00");
   * x-= Time("16:30:00");
   *
   * In these cases, we need to subtract a day from the resulting
   * DateTime.
   */
  if (second_diff < 0)
    _days--;

  _hours= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_HOUR;
  second_diff%= DRIZZLE_SECONDS_IN_HOUR;
  _minutes= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_MINUTE;
  second_diff%= DRIZZLE_SECONDS_IN_MINUTE;
  _seconds= (uint32_t) second_diff;

  /* Handle the microsecond precision */
  int64_t microsecond_diff= _useconds - rhs._useconds;
  if (microsecond_diff < 0)
  {
    microsecond_diff= (-1 * microsecond_diff);
    _seconds--;
  }
  _useconds= (uint32_t) microsecond_diff;
  /** 
   * @TODO Once exceptions are supported, we should raise an error here if
   *       the result Time is not valid?
   */
  return *this;
}

/**
 * We can add/subtract two Dates to/from each other.  The result
 * is always another Date instance.
 */
const Date Date::operator-(const Date &rhs)
{
  /* Figure out the difference in days between the two dates */
  int64_t day_left= julian_day_number_from_gregorian_date(_years, _months, _days);
  int64_t day_right= julian_day_number_from_gregorian_date(rhs._years, rhs._months, rhs._days);
  int64_t day_diff= day_left - day_right;

  Date result;
  /* Now re-compose the Date's structure from the resulting Julian Day Number */
  gregorian_date_from_julian_day_number(day_diff, &result._years, &result._months, &result._days);
  return result;
}
const Date Date::operator+(const Date &rhs)
{
  /* 
   * Figure out the new Julian Day Number by adding the JDNs of both
   * dates together.
   */
  int64_t day_left= julian_day_number_from_gregorian_date(_years, _months, _days);
  int64_t day_right= julian_day_number_from_gregorian_date(rhs._years, rhs._months, rhs._days);
  int64_t day_diff= day_left + day_right;

  /** @TODO Need an exception check here for bounds of JDN... */

  Date result;
  /* Now re-compose the Date's structure from the resulting Julian Day Number */
  gregorian_date_from_julian_day_number(day_diff, &result._years, &result._months, &result._days);
  return result;
}
/* Similar to the above, but we add/subtract the right side to this object itself */
Date& Date::operator-=(const Date &rhs)
{
  int64_t day_left= julian_day_number_from_gregorian_date(_years, _months, _days);
  int64_t day_right= julian_day_number_from_gregorian_date(rhs._years, rhs._months, rhs._days);
  int64_t day_diff= day_left - day_right;

  /* Now re-compose the Date's structure from the resulting Julian Day Number */
  gregorian_date_from_julian_day_number(day_diff, &_years, &_months, &_days);
  return *this;
}
Date& Date::operator+=(const Date &rhs)
{
  /* 
   * Figure out the new Julian Day Number by adding the JDNs of both
   * dates together.
   */
  int64_t day_left= julian_day_number_from_gregorian_date(_years, _months, _days);
  int64_t day_right= julian_day_number_from_gregorian_date(rhs._years, rhs._months, rhs._days);
  int64_t day_diff= day_left + day_right;

  /** @TODO Need an exception check here for bounds of JDN... */

  /* Now re-compose the Date's structure from the resulting Julian Day Number */
  gregorian_date_from_julian_day_number(day_diff, &_years, &_months, &_days);
  return *this;
}

Date& Date::operator=(const DateTime &rhs)
{
  /* Only copy the Date components of the assigned DateTime... */
  _years= rhs._years;
  _months= rhs._months;
  _days= rhs._days;
  /* Zero-out everything else.. */
  _hours= _minutes= _seconds= _useconds= _nseconds= 0;
  return *this;
}

/**
 * We can add/subtract two DateTimes to/from each other.  The result
 * is always another DateTime instance.
 */
const Date Date::operator-(const DateTime &rhs)
{
  /* Figure out the difference in days between the two dates. */
  int64_t day_left= julian_day_number_from_gregorian_date(_years, _months, _days);
  int64_t day_right= julian_day_number_from_gregorian_date(rhs._years, rhs._months, rhs._days);
  int64_t day_diff= day_left - day_right;

  DateTime result;
  /* Now re-compose the Date's structure from the resulting Julian Day Number */
  gregorian_date_from_julian_day_number(day_diff, &result._years, &result._months, &result._days);

  /* And now handle the time components */
  int64_t second_diff= _cumulative_seconds_in_time() - rhs._cumulative_seconds_in_time();

  /* 
   * The resulting diff might be negative.  If it is, that means that 
   * we have subtracting a larger time piece from the datetime, like so:
   *
   * x = DateTime("2007-06-09 09:30:00");
   * x-= Time("16:30:00");
   *
   * In these cases, we need to subtract a day from the resulting
   * DateTime.
   */
  if (second_diff < 0)
    _days--;

  result._hours= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_HOUR;
  second_diff%= DRIZZLE_SECONDS_IN_HOUR;
  result._minutes= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_MINUTE;
  second_diff%= DRIZZLE_SECONDS_IN_MINUTE;
  result._seconds= (uint32_t) second_diff;

  /* Handle the microsecond precision */
  int64_t microsecond_diff= _useconds - rhs._useconds;
  if (microsecond_diff < 0)
  {
    microsecond_diff= (-1 * microsecond_diff);
    result._seconds--;
  }
  result._useconds= (uint32_t) microsecond_diff;

  return result;
}
const Date Date::operator+(const DateTime &rhs)
{
  /*
   * Figure out the new Julian Day Number by adding the JDNs of both
   * dates together.
   */
  int64_t day_left= julian_day_number_from_gregorian_date(_years, _months, _days);
  int64_t day_right= julian_day_number_from_gregorian_date(rhs._years, rhs._months, rhs._days);
  int64_t day_diff= day_left + day_right;

  /** @TODO Need an exception check here for bounds of JDN... */

  DateTime result;
  /* Now re-compose the Date's structure from the resulting Julian Day Number */
  gregorian_date_from_julian_day_number(day_diff, &result._years, &result._months, &result._days);

  /* And now handle the time components */
  int64_t second_diff= _cumulative_seconds_in_time() + rhs._cumulative_seconds_in_time();

  /* 
   * The resulting seconds might be more than a day.  If do, 
   * adjust our resulting days up 1.
   */
  if (second_diff >= DRIZZLE_SECONDS_IN_DAY)
  {
    result._days++;
    second_diff%= DRIZZLE_SECONDS_IN_DAY;
  }

  result._hours= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_HOUR;
  second_diff%= DRIZZLE_SECONDS_IN_HOUR;
  result._minutes= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_MINUTE;
  second_diff%= DRIZZLE_SECONDS_IN_MINUTE;
  result._seconds= (uint32_t) second_diff;

  /* Handle the microsecond precision */
  int64_t microsecond_diff= _useconds - rhs._useconds;
  if (microsecond_diff < 0)
  {
    microsecond_diff= (-1 * microsecond_diff);
    result._seconds--;
  }
  result._useconds= (uint32_t) microsecond_diff;

  return result;
}
/* Similar to the above, but we add/subtract the right side to this object itself */
Date& Date::operator-=(const DateTime &rhs)
{
  /* Figure out the difference in days between the two dates.  */
  int64_t day_left= julian_day_number_from_gregorian_date(_years, _months, _days);
  int64_t day_right= julian_day_number_from_gregorian_date(rhs._years, rhs._months, rhs._days);
  int64_t day_diff= day_left - day_right;

  /* Now re-compose the Date's structure from the ng Julian Day Number */
  gregorian_date_from_julian_day_number(day_diff, &_years, &_months, &_days);

  /* And now handle the time components */
  int64_t second_diff= _cumulative_seconds_in_time() - rhs._cumulative_seconds_in_time();

  /* 
   * The resulting diff might be negative.  If it is, that means that 
   * we have subtracting a larger time piece from the datetime, like so:
   *
   * x = DateTime("2007-06-09 09:30:00");
   * x-= Time("16:30:00");
   *
   * In these cases, we need to subtract a day from the ng
   * DateTime.
   */
  if (second_diff < 0)
    _days--;

  _hours= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_HOUR;
  second_diff%= DRIZZLE_SECONDS_IN_HOUR;
  _minutes= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_MINUTE;
  second_diff%= DRIZZLE_SECONDS_IN_MINUTE;
  _seconds= (uint32_t) second_diff;

  /* Handle the microsecond precision */
  int64_t microsecond_diff= _useconds - rhs._useconds;
  if (microsecond_diff < 0)
  {
    microsecond_diff= (-1 * microsecond_diff);
    _seconds--;
  }
  _useconds= (uint32_t) microsecond_diff;

  return *this;
}
Date& Date::operator+=(const DateTime &rhs)
{
  /* 
   * Figure out the new Julian Day Number by adding the JDNs of both
   * dates together.
   */
  int64_t day_left= julian_day_number_from_gregorian_date(_years, _months, _days);
  int64_t day_right= julian_day_number_from_gregorian_date(rhs._years, rhs._months, rhs._days);
  int64_t day_diff= day_left + day_right;

  /** @TODO Need an exception check here for bounds of JDN... */

  /* Now re-compose the Date's structure from the ng Julian Day Number */
  gregorian_date_from_julian_day_number(day_diff, &_years, &_months, &_days);

  /* And now handle the time components */
  int64_t second_diff= _cumulative_seconds_in_time() + rhs._cumulative_seconds_in_time();

  /* 
   * The resulting seconds might be more than a day.  If do, 
   * adjust our ng days up 1.
   */
  if (second_diff >= DRIZZLE_SECONDS_IN_DAY)
  {
    _days++;
    second_diff%= DRIZZLE_SECONDS_IN_DAY;
  }

  _hours= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_HOUR;
  second_diff%= DRIZZLE_SECONDS_IN_HOUR;
  _minutes= (uint32_t) second_diff / DRIZZLE_SECONDS_IN_MINUTE;
  second_diff%= DRIZZLE_SECONDS_IN_MINUTE;
  _seconds= (uint32_t) second_diff;

  /* Handle the microsecond precision */
  int64_t microsecond_diff= _useconds - rhs._useconds;
  if (microsecond_diff < 0)
  {
    microsecond_diff= (-1 * microsecond_diff);
    _seconds--;
  }
  _useconds= (uint32_t) microsecond_diff;

  return *this;
}
#ifdef NOTYETIMPLEMENTED
Date& Date::operator+=(const TemporalIntervalYear &rhs)
{
  /* Simple one...add the years and adjust for any leaps */
  int64_t new_years= _years;
  new_years+= rhs._years;
  if (new_years > DRIZZLE_MAX_YEARS_SQL)
  {
    /* 
     * Set everything to zero. We got an overflow.
     * @TODO Exceptions would be great here...
     */
    _reset();
    _overflow= true;
    return *this;
  }
  _years= (uint32_t) new_years;
  if (_months == 2 && _days == 29 && days_in_gregorian_year_month(_years, _months) != 366)
    _days= 28;
  return *this;
} 

Date& Date::operator-=(const TemporalIntervalYear &rhs)
{
  /* Simple one...subtract the years and adjust for any leaps */
  int64_t new_years= _years;
  new_years-= rhs._years;
  if (new_years < 0)
  {
    /* 
     * Set everything to zero. We got an overflow.
     * @TODO Exceptions would be great here...
     */
    _reset();
    _overflow= true;
    return *this;
  }
  _years= (uint32_t) new_years;
  if (_months == 2 && _days == 29 && days_in_gregorian_year_month(_years, _months) != 366)
    _days= 28;
  return *this;
} 

Date& Date::operator+=(const TemporalIntervalDayOrWeek &rhs)
{
  /* Simple one...add the days */
  int64_t julian_day= julian_day_number_from_gregorian_date(_years, _months, _days) + rhs._days;
  gregorian_date_from_julian_day_number(julian_day, &_years, &_months, &_days);
  return *this;
} 

Date& Date::operator-=(const TemporalIntervalDayOrWeek &rhs)
{
  /* Simple one...subtract the days */
  int64_t julian_day= julian_day_number_from_gregorian_date(_years, _months, _days) - rhs._days;
  gregorian_date_from_julian_day_number(julian_day, &_years, &_months, &_days);
  return *this;
} 

Date& Date::operator+=(const TemporalIntervalYearMonth &rhs)
{
  /* Simple one...add the months in the period adjust */
  int64_t period= (_years * 12) + (rhs._years * 12) + (_months - 1) + rhs._months;
  int64_t new_years= (period / 12);
  if (new_years > DRIZZLE_MAX_YEARS_SQL)
  {
    /* 
     * Set everything to zero. We got an overflow.
     * @TODO Exceptions would be great here...
     */
    _reset();
    _overflow= true;
    return *this;
  }
  _years= (uint32_t) new_years;
  _months= (uint32_t) (period % 12) + 1;
  
  /* Adjust day if the new month doesn't have enough days */
  uint32_t days_in_new_month= days_in_gregorian_year_month(_years, _months);
  if (_days > days_in_new_month)
    _days= days_in_new_month;
  return *this;
} 

Date& Date::operator-=(const TemporalIntervalYearMonth &rhs)
{
  /* Simple one...subtract the months in the period and adjust */
  int64_t period= (_years * 12) - (rhs._years * 12) + (_months - 1) - rhs._months;
  int64_t new_years= (period / 12);
  if (new_years < 0)
  {
    /* 
     * Set everything to zero. We got an overflow.
     * @TODO Exceptions would be great here...
     */
    _reset();
    _overflow= true;
    return *this;
  }
  _years= (uint32_t) (period / 12);
  _months= (uint32_t) (period % 12) + 1;
  
  /* Adjust day if the new month doesn't have enough days */
  uint32_t days_in_new_month= days_in_gregorian_year_month(_years, _months);
  if (_days > days_in_new_month)
    _days= days_in_new_month;
  return *this;
} 

Date& Date::operator+=(const TemporalIntervalDayOrLess &rhs)
{
  /* 
   * Convert the temporal and the interval into a number of 
   * microseconds, then add them together and convert the
   * resulting microseconds back into a broken-down temporal
   * component.
   */
  int64_t new_seconds;
  int64_t new_microseconds;
  int64_t extra_sec;
  int64_t new_days;
  new_microseconds= _useconds + rhs._useconds;
  extra_sec= new_microseconds / INT64_C(1000000);
  new_microseconds= new_microseconds % INT64_C(1000000);

  new_seconds= ((_days - 1) * 3600 * 24) + (_hours * 3600) + (_minutes * 60) + _seconds;
  new_seconds+= (rhs._days * 3600 * 24) + (rhs._hours * 3600) + (rhs._minutes * 60) + rhs._seconds;
  new_seconds+= extra_sec;

  if (new_microseconds < 0)
  {
    new_microseconds+= INT64_C(1000000);
    new_seconds--;
  }
  
  new_days= new_seconds / (3600 * 24L);
  new_seconds-= new_days * 3600 * 24L;
  if (new_seconds < 0)
  {
    new_days--;
    new_seconds+= 3600 * 24L;
  }
  _useconds= (uint32_t) new_microseconds;
  _seconds= (uint32_t) (new_seconds % 60);
  _minutes= (uint32_t) ((new_seconds / 60) % 60);
  _hours= (uint32_t) (new_seconds / 3600);
  int64_t julian_day= julian_day_number_from_gregorian_date(_years, _months, 1) + new_days;
  gregorian_date_from_julian_day_number(julian_day, &_years, &_months, &_days);
  return *this;
}

Date& Date::operator-=(const TemporalIntervalDayOrLess &rhs)
{
  /* 
   * Convert the temporal and the interval into a number of 
   * microseconds, then subtract them from each other and convert 
   * the resulting microseconds back into a broken-down temporal
   * component.
   */
  int64_t new_seconds;
  int64_t new_microseconds;
  int64_t extra_sec;
  int64_t new_days;
  new_microseconds= _useconds - rhs._useconds;
  extra_sec= new_microseconds / INT64_C(1000000);
  new_microseconds= new_microseconds % INT64_C(1000000);

  new_seconds= ((_days - 1) * 3600 * 24) + (_hours * 3600) + (_minutes * 60) + _seconds;
  new_seconds-= (rhs._days * 3600 * 24) + (rhs._hours * 3600) + (rhs._minutes * 60) + rhs._seconds;
  new_seconds+= extra_sec;

  if (new_microseconds < 0)
  {
    new_microseconds+= INT64_C(1000000);
    new_seconds--;
  }
  
  new_days= new_seconds / (3600 * 24L);
  new_seconds-= new_days * 3600 * 24L;
  if (new_seconds < 0)
  {
    new_days--;
    new_seconds+= 3600 * 24L;
  }
  _useconds= (uint32_t) new_microseconds;
  _seconds= (uint32_t) (new_seconds % 60);
  _minutes= (uint32_t) ((new_seconds / 60) % 60);
  _hours= (uint32_t) (new_seconds / 3600);
  int64_t julian_day= julian_day_number_from_gregorian_date(_years, _months, 1) + new_days;
  gregorian_date_from_julian_day_number(julian_day, &_years, &_months, &_days);
  return *this;
}
#endif /* NOTYETIMPLEMENTED */
/*
 * Comparison operators between a Date and a Timestamp
 */
bool Date::operator==(const Timestamp& rhs)
{
  return (_years == rhs._years && _months == rhs._months && _days == rhs._days);
}
bool Date::operator!=(const Timestamp& rhs)
{
  return ! (*this == rhs);
}
bool Date::operator<(const Timestamp& rhs)
{
  if (_years < rhs._years)
    return true;
  if (_years > rhs._years)
    return false;
  /* In same year */
  if (_months < rhs._months)
    return true;
  if (_months > rhs._months)
    return false;
  /* Same month */
  return _days < rhs._days;
}
bool Date::operator<=(const Timestamp& rhs)
{
  return (*this < rhs || *this == rhs);
}
bool Date::operator>(const Timestamp& rhs)
{
  return ! (*this < rhs);
}
bool Date::operator>=(const Timestamp& rhs)
{
  return ! (*this <= rhs);
}
/*
 * Comparison operators between a Timestamp and a Date
 */
bool Timestamp::operator==(const Date& rhs)
{
  return (_years == rhs._years && _months == rhs._months && _days == rhs._days);
}
bool Timestamp::operator!=(const Date& rhs)
{
  return ! (*this == rhs);
}
bool Timestamp::operator<(const Date& rhs)
{
  if (_years < rhs._years)
    return true;
  if (_years > rhs._years)
    return false;
  /* In same year */
  if (_months < rhs._months)
    return true;
  if (_months > rhs._months)
    return false;
  /* Same month */
  return _days < rhs._days;
}
bool Timestamp::operator<=(const Date& rhs)
{
  return (*this < rhs || *this == rhs);
}
bool Timestamp::operator>(const Date& rhs)
{
  return ! (*this < rhs);
}
bool Timestamp::operator>=(const Date& rhs)
{
  return ! (*this <= rhs);
}
/*
 * Comparison operators between a Timestamp and a DateTime
 */
bool Timestamp::operator==(const DateTime& rhs)
{
  return (_years == rhs._years && _months == rhs._months && _days == rhs._days
          && _hours == rhs._hours && _minutes == rhs._minutes && _seconds == rhs._seconds);
}
bool Timestamp::operator!=(const DateTime& rhs)
{
  return ! (*this == rhs);
}
bool Timestamp::operator<(const DateTime& rhs)
{
  if (_years < rhs._years)
    return true;
  if (_years > rhs._years)
    return false;
  /* In same year */
  if (_months < rhs._months)
    return true;
  if (_months > rhs._months)
    return false;
  /* Same month */
  if (_days < rhs._days)
    return true;
  if (_days > rhs._days)
     return false;
  /* Same day */
  if (_hours < rhs._hours)
    return true;
  if (_hours > rhs._hours)
    return false;
  /* Same hour */
  if (_minutes < rhs._minutes)
    return true;
  if (_minutes > rhs._minutes)
    return false;
  /* Same minute */
  return _seconds < rhs._seconds;
}
bool Timestamp::operator<=(const DateTime& rhs)
{
  return (*this < rhs || *this == rhs);
}
bool Timestamp::operator>(const DateTime& rhs)
{
  return ! (*this < rhs);
}
bool Timestamp::operator>=(const DateTime& rhs)
{
  return ! (*this <= rhs);
}
/*
 * Comparison operators between two Timestamps
 */
bool Timestamp::operator==(const Timestamp& rhs)
{
  return (_epoch_seconds == rhs._epoch_seconds);
}
bool Timestamp::operator!=(const Timestamp& rhs)
{
  return ! (*this == rhs);
}
bool Timestamp::operator<(const Timestamp& rhs)
{
  return (_epoch_seconds < rhs._epoch_seconds);
}
bool Timestamp::operator<=(const Timestamp& rhs)
{
  return (_epoch_seconds <= rhs._epoch_seconds);
}
bool Timestamp::operator>(const Timestamp& rhs)
{
  return ! (*this < rhs);
}
bool Timestamp::operator>=(const Timestamp& rhs)
{
  return ! (*this <= rhs);
}

/**
 * Push the contents of the timestamp into the output stream
 * as a formatted Timestamp value.
 *
 * @TODO This unfortunately fails in a weird way...even with std::noskipws, 
 * the output stream only reads up to the space in the string... :(
 */
std::ostream& operator<<(std::ostream& os, const Timestamp& subject)
{
  return os << subject.years() << '-' 
            << std::setw(2) << std::setfill('0') << subject.months() << '-'
            << std::setw(2) << std::setfill('0') << subject.days() << ' '
            << std::setw(2) << std::setfill('0') << subject.hours() << ':'
            << std::setw(2) << std::setfill('0') << subject.minutes() << ':'
            << std::setw(2) << std::setfill('0') << subject.seconds();
}

bool Time::from_string(const char *from, size_t from_len)
{
  /*
   * Loop through the known time formats and see if
   * there is a match.
   */
  bool matched= false;
  TemporalFormat *current_format;
  std::vector<TemporalFormat *>::iterator current= known_time_formats.begin();

  while (current != known_time_formats.end())
  {
    current_format= *current;
    if (current_format->matches(from, from_len, this))
    {
      matched= true;
      break;
    }
    current++;
  }

  if (! matched)
    return false;
  else
    return is_valid();
}

void Time::to_string(char *to, size_t *to_len) const
{
  *to_len= sprintf(to
                 , "%02" PRIu32 ":%02" PRIu32 ":%02" PRIu32
                 , _hours
                 , _minutes
                 , _seconds);
}

void Date::to_string(char *to, size_t *to_len) const
{
  *to_len= sprintf(to
                 , "%04" PRIu32 "-%02" PRIu32 "-%02" PRIu32
                 , _years
                 , _months
                 , _days);
}

void DateTime::to_string(char *to, size_t *to_len) const
{
  /* If the temporal has a microsecond component, use a slightly different output */
  if (_useconds == 0)
  {
    *to_len= sprintf(to
                  , "%04" PRIu32 "-%02" PRIu32 "-%02" PRIu32 " %02" PRIu32 ":%02" PRIu32 ":%02" PRIu32
                  , _years
                  , _months
                  , _days
                  , _hours
                  , _minutes
                  , _seconds);
  }
  else
  {
    *to_len= sprintf(to
                  , "%04" PRIu32 "-%02" PRIu32 "-%02" PRIu32 " %02" PRIu32 ":%02" PRIu32 ":%02" PRIu32 ".%06" PRIu32
                  , _years
                  , _months
                  , _days
                  , _hours
                  , _minutes
                  , _seconds
                  , _useconds);
  }
}

void MicroTimestamp::to_string(char *to, size_t *to_len) const
{
  *to_len= sprintf(to
                 , "%04" PRIu32 "-%02" PRIu32 "-%02" PRIu32 " %02" PRIu32 ":%02" PRIu32 ":%02" PRIu32 ".%06" PRIu32
                 , _years
                 , _months
                 , _days
                 , _hours
                 , _minutes
                 , _seconds
                 , _useconds);
}

void Time::to_decimal(my_decimal *to) const
{
  int64_t time_portion= (((_hours * 100L) + _minutes) * 100L) + _seconds;
  (void) int2my_decimal(E_DEC_FATAL_ERROR, time_portion, false, to);
  if (_useconds > 0)
  {
    to->buf[(to->intg-1) / 9 + 1]= _useconds * 1000;
    to->frac= 6;
  }
}

void Date::to_decimal(my_decimal *to) const
{
  int64_t date_portion= (((_years * 100L) + _months) * 100L) + _days;
  (void) int2my_decimal(E_DEC_FATAL_ERROR, date_portion, false, to);
}

void DateTime::to_decimal(my_decimal *to) const
{
  int64_t date_portion= (((_years * 100L) + _months) * 100L) + _days;
  int64_t time_portion= (((((date_portion * 100L) + _hours) * 100L) + _minutes) * 100L) + _seconds;
  (void) int2my_decimal(E_DEC_FATAL_ERROR, time_portion, false, to);
  if (_useconds > 0)
  {
    to->buf[(to->intg-1) / 9 + 1]= _useconds * 1000;
    to->frac= 6;
  }
}

void Date::to_int64_t(int64_t *to) const
{
  *to= (_years * INT32_C(10000)) 
     + (_months * INT32_C(100)) 
     + _days;
}

void Date::to_int32_t(int32_t *to) const
{
  *to= (_years * INT32_C(10000)) 
     + (_months * INT32_C(100)) 
     + _days;
}

void Time::to_int32_t(int32_t *to) const
{
  *to= (_hours * INT32_C(10000)) 
     + (_minutes * INT32_C(100)) 
     + _seconds;
}

void DateTime::to_int64_t(int64_t *to) const
{
  *to= ((
       (_years * INT64_C(10000)) 
     + (_months * INT64_C(100)) 
     + _days
       ) * INT64_C(1000000))
     + (
       (_hours * INT64_C(10000)) 
     + (_minutes * INT64_C(100) )
     + _seconds
     );
}

void Date::to_tm(struct tm *to) const
{
  to->tm_sec= 0;
  to->tm_min= 0;
  to->tm_hour= 0;
  to->tm_mday= _days; /* Drizzle format uses ordinal, standard tm does too! */
  to->tm_mon= _months - 1; /* Drizzle format uses ordinal, standard tm does NOT! */
  to->tm_year= _years - 1900;
}

void DateTime::to_tm(struct tm *to) const
{
  to->tm_sec= _seconds;
  to->tm_min= _minutes;
  to->tm_hour= _hours;
  to->tm_mday= _days; /* Drizzle format uses ordinal, standard tm does too! */
  to->tm_mon= _months - 1; /* Drizzle format uses ordinal, standard tm does NOT! */
  to->tm_year= _years - 1900;
}

bool Date::from_julian_day_number(const int64_t from)
{
  gregorian_date_from_julian_day_number(from, &_years, &_months, &_days);
  return is_valid();
}

void Date::to_julian_day_number(int64_t *to) const
{
  *to= julian_day_number_from_gregorian_date(_years, _months, _days);
}

/**
 * Ignore overflow and pass-through to DateTime::from_int64_t()
 */
bool Date::from_int32_t(const int32_t from)
{
  return ((DateTime *) this)->from_int64_t((int64_t) from);
}

/**
 * Attempt to interpret the supplied 4-byte integer as
 * a TIME value in the format HHmmSS
 */
bool Time::from_int32_t(const int32_t from)
{
  uint32_t copy_from= (uint32_t) from;
  _hours= copy_from % INT32_C(10000);
  _minutes= copy_from % INT32_C(100);
  _seconds= copy_from & 3; /* Masks off all but last 2 digits */
  return is_valid();
}

/**
 * We try to intepret the incoming number as a datetime "string".
 * This is pretty much a hack for usability, but keeps us compatible
 * with MySQL.
 */
bool DateTime::from_int64_t(const int64_t from)
{
  int64_t copy_from= from;
  int64_t part1;
  int64_t part2;

  if (copy_from == 0LL)
    return false;

  if (copy_from < 10000101000000LL)
  {
    if (copy_from < 101)
      return false;
    else if (copy_from <= (DRIZZLE_YY_PART_YEAR-1)*10000L+1231L)
      copy_from= (copy_from+20000000L)*1000000L;                 /* YYMMDD, year: 2000-2069 */
    else if (copy_from < (DRIZZLE_YY_PART_YEAR)*10000L+101L)
      return false;
    else if (copy_from <= 991231L)
      copy_from= (copy_from+19000000L)*1000000L;                 /* YYMMDD, year: 1970-1999 */
    else if (copy_from < 10000101L)
      return false;
    else if (copy_from <= 99991231L)
      copy_from= copy_from*1000000L;
    else if (copy_from < 101000000L)
      return false;
    else if (copy_from <= (DRIZZLE_YY_PART_YEAR-1) * 10000000000LL + 1231235959LL)
      copy_from= copy_from + 20000000000000LL;                   /* YYMMDDHHMMSS, 2000-2069 */
    else if (copy_from <  DRIZZLE_YY_PART_YEAR * 10000000000LL + 101000000LL)
      return false;
    else if (copy_from <= 991231235959LL)
      copy_from= copy_from + 19000000000000LL;		/* YYMMDDHHMMSS, 1970-1999 */
  }

  part1= (int64_t) (copy_from / 1000000LL);
  part2= (int64_t) (copy_from - (int64_t) part1 * 1000000LL);
  _years=  (uint32_t) (part1/10000L);  
  
  part1%=10000L;
  _months= (uint32_t) part1 / 100;
  _days=   (uint32_t) part1 % 100;
  _hours=  (uint32_t) (part2/10000L);  

  part2%=10000L;
  _minutes= (uint32_t) part2 / 100;
  _seconds= (uint32_t) part2 % 100;

  set_epoch_seconds();
  return is_valid();
}

bool Date::in_unix_epoch() const
{
  return in_unix_epoch_range(_years, _months, _days, 0, 0, 0);
}

bool DateTime::in_unix_epoch() const
{
  return in_unix_epoch_range(_years, _months, _days, _hours, _minutes, _seconds);
}

bool Date::from_tm(const struct tm *from)
{
  _years= 1900 + from->tm_year;
  _months= 1 + from->tm_mon; /* Month is NOT ordinal for struct tm! */
  _days= from->tm_mday; /* Day IS ordinal for struct tm */
  _hours= from->tm_hour;
  _minutes= from->tm_min;
  _seconds= from->tm_sec;
  /* Set hires precision to zero */
  _useconds= 0;
  _nseconds= 0;

  set_epoch_seconds();
  return is_valid();
}

/* 
 * We convert as if it's a Datetime, then simply
 * drop the date portions...
 */
bool Time::from_time_t(const time_t from)
{
  struct tm broken_time;
  struct tm *result;

  result= gmtime_r(&from, &broken_time);
  if (result != NULL)
  {
    _years= 0;
    _months= 0;
    _days= 0;
    _hours= broken_time.tm_hour;
    _minutes= broken_time.tm_min;
    _seconds= broken_time.tm_sec;
    _epoch_seconds= 0; /* Don't store the time_t, since we only use part of it */
    /* Set hires precision to zero */
    _useconds= 0;
    _nseconds= 0;
    return true; /* Always true... */
  }
  else 
    return false;
}

bool Date::from_time_t(const time_t from)
{
  struct tm broken_time;
  struct tm *result;

  result= gmtime_r(&from, &broken_time);
  if (result != NULL)
  {
    _years= 1900 + broken_time.tm_year;
    _months= 1 + broken_time.tm_mon; /* Month is NOT ordinal for struct tm! */
    _days= broken_time.tm_mday; /* Day IS ordinal for struct tm */
    _hours= 0;
    _minutes= 0;
    _seconds= 0;
    _epoch_seconds= 0; /* Don't store the time_t, since we only use part of it */
    /* Set hires precision to zero */
    _useconds= 0;
    _nseconds= 0;
    return is_valid();
  }
  else 
    return false;
}

bool DateTime::from_time_t(const time_t from)
{
  struct tm broken_time;
  struct tm *result;

  result= gmtime_r(&from, &broken_time);
  if (result != NULL)
  {
    _years= 1900 + broken_time.tm_year;
    _months= 1 + broken_time.tm_mon; /* Month is NOT ordinal for struct tm! */
    _days= broken_time.tm_mday; /* Day IS ordinal for struct tm */
    _hours= broken_time.tm_hour;
    _minutes= broken_time.tm_min;
    _seconds= broken_time.tm_sec;
    _epoch_seconds= from;
    /* Set hires precision to zero */
    _useconds= 0;
    _nseconds= 0;
    return is_valid();
  }
  else 
    return false;
}

void Date::to_time_t(time_t *to) const
{
  if (in_unix_epoch())
    *to= _epoch_seconds;
  *to= 0;
}

void Timestamp::to_time_t(time_t *to) const
{
  *to= _epoch_seconds;
}

void MicroTimestamp::to_timeval(struct timeval *to) const
{
  to->tv_sec= _epoch_seconds;
  to->tv_usec= _useconds;
}

void NanoTimestamp::to_timespec(struct timespec *to) const
{
  to->tv_sec= _epoch_seconds;
  to->tv_nsec= _nseconds;
}

bool Date::is_valid() const
{
  return (_years >= DRIZZLE_MIN_YEARS_SQL && _years <= DRIZZLE_MAX_YEARS_SQL)
      && (_months >= 1 && _months <= 12)
      && (_days >= 1 && _days <= days_in_gregorian_year_month(_years, _months));
}

bool Time::is_valid() const
{
  return (_years == 0)
      && (_months == 0)
      && (_days == 0)
      && (_hours <= 23)
      && (_minutes <= 59)
      && (_seconds <= 59); /* No Leap second... TIME is for elapsed time... */
}

bool DateTime::is_valid() const
{
  return (_years >= DRIZZLE_MIN_YEARS_SQL && _years <= DRIZZLE_MAX_YEARS_SQL)
      && (_months >= 1 && _months <= 12)
      && (_days >= 1 && _days <= days_in_gregorian_year_month(_years, _months))
      && (_hours <= 23)
      && (_minutes <= 59)
      && (_seconds <= 61); /* Leap second... */
}

bool Timestamp::is_valid() const
{
  return DateTime::is_valid() 
      && in_unix_epoch_range(_years, _months, _days, _hours, _minutes, _seconds);
}

bool MicroTimestamp::is_valid() const
{
  return Timestamp::is_valid()
      && (_useconds <= UINT32_C(999999));
}

bool NanoTimestamp::is_valid() const
{
  return Timestamp::is_valid()
      && (_useconds <= UINT32_C(999999))
      && (_nseconds <= UINT32_C(999999999));
}

} /* end namespace drizzled */
