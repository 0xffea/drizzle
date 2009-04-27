/* Copyright (C) 2003 MySQL AB

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


#include <drizzled/server_includes.h>
#include "transparent_file.h"

Transparent_file::Transparent_file() : lower_bound(0), buff_size(IO_SIZE)
{
  buff= (unsigned char *) malloc(buff_size*sizeof(unsigned char));
}

Transparent_file::~Transparent_file()
{
  free((unsigned char*)buff);
}

void Transparent_file::init_buff(File filedes_arg)
{
  filedes= filedes_arg;
  /* read the beginning of the file */
  lower_bound= 0;
  lseek(filedes, 0, SEEK_SET);
  if (filedes && buff)
    upper_bound= my_read(filedes, buff, buff_size, MYF(0));
}

unsigned char *Transparent_file::ptr()
{
  return buff;
}

off_t Transparent_file::start()
{
  return lower_bound;
}

off_t Transparent_file::end()
{
  return upper_bound;
}

off_t Transparent_file::read_next()
{
  size_t bytes_read;

  /*
     No need to seek here, as the file managed by Transparent_file class
     always points to upper_bound byte
  */
  if ((bytes_read= my_read(filedes, buff, buff_size, MYF(0))) == MY_FILE_ERROR)
    return (off_t) -1;

  /* end of file */
  if (!bytes_read)
    return (off_t) -1;

  lower_bound= upper_bound;
  upper_bound+= bytes_read;

  return lower_bound;
}


char Transparent_file::get_value(off_t offset)
{
  size_t bytes_read;

  /* check boundaries */
  if ((lower_bound <= offset) && (offset < upper_bound))
    return buff[offset - lower_bound];

  lseek(filedes, offset, SEEK_SET);
  /* read appropriate portion of the file */
  if ((bytes_read= my_read(filedes, buff, buff_size,
                           MYF(0))) == MY_FILE_ERROR)
    return 0;

  lower_bound= offset;
  upper_bound= lower_bound + bytes_read;

  /* end of file */
  if (upper_bound == offset)
    return 0;

  return buff[0];
}