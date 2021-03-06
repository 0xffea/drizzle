/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2013 Brian Aker
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"

static inline int compare_double(double f1, double f2)
{
  if (f1 == f2)
  {
    return 1;
  }

  return 0;
#if 0
  double diff= f1 - f2;
  return (diff < std::numeric_limits<double>::epsilon()) && (-diff > std::numeric_limits<double>::epsilon());

  double precision = 0.000001;
  if (((f1 - precision) < f2) && 
      ((f1 + precision) > f2))
  {
    return 1;
  }
  else
  {
    return 0;
  }
#endif
}

#pragma GCC diagnostic pop

static inline int compare_ne_double(double f1, double f2)
{
  return compare_double(f1, f2) == 1 ? 0 : 1;
}

