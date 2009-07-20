/* Copyright (C) 2000 MySQL AB

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

/* readline for batch mode */

#include <drizzled/global.h>
#include <mysys/my_sys.h>
#include "client/linebuffer.h"

#include <vector>

using namespace std;

LineBuffer::LineBuffer(uint32_t my_max_size,FILE *my_file)
  :
    file(my_file),
    line(),
    max_size(my_max_size),
    eof(false)
{
  line.reserve(max_size);
}

void LineBuffer::addString(const string &str)
{
  buffer << str << endl;
}

char *LineBuffer::readline()
{
  uint32_t read_count;

  if (file && !eof)
  {
    if ((read_count=my_read(fileno(file),
                            (unsigned char *) (&line[0]),
                            max_size-1,MYF(MY_WME))))
    {
      line[read_count+1]= '\0';
      buffer << &line[0];
    }
    else
      eof= true;
  }

  buffer.getline(&line[0],max_size);

  if (buffer.eof())
    return 0;
  else
    return &line[0];
}

