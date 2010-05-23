/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (c) 2006 MySQL AB
 *  Copyright (c) 2009 Sun Microsystems, Inc.
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

#include "config.h"
#include CSTDINT_H
#include <iostream>
#include <string>
using namespace std;
#include <boost/program_options.hpp>
namespace po= boost::program_options;
#include "azio.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include "drizzled/charset_info.h"
#include "drizzled/internal/m_string.h"
#include "drizzled/option.h"

#define SHOW_VERSION "0.1"

using namespace drizzled;

string opt_tmpdir;
uint64_t new_auto_increment_value;
static const char *load_default_groups[]= { "archive_reader", 0 };
static char **default_argv;
bool opt_check, 
  opt_force,
  opt_backup, 
  opt_extract_table_message, 
  opt_silent,
  opt_quiet,
  opt_quick,
  opt_autoincrement;

int main(int argc, char *argv[])
{
try
{
  po::options_description long_options("Allowed Options");
  long_options.add_options()
  ("back_up,b",po::value<bool>(&opt_backup)->default_value(false)->zero_tokens(),
  "Make a backup of an archive table.")
  ("check,c",po::value<bool>(&opt_check)->default_value(false)->zero_tokens(),
  "Check table for errors")
  ("extract-table-message,e",po::value<bool>(&opt_extract_table_message)->default_value(false)->zero_tokens(),
  "Extract the table protobuf message.")
  ("force,f",po::value<bool>(&opt_force)->default_value(false)->zero_tokens(),
  "Restart with -r if there are any errors in the table")
  ("help,?","Display this help and exit")
  ("quick,q",po::value<bool>(&opt_quick)->default_value(false)->zero_tokens(),
  "Faster repair but not modifying the data")
  ("repair,r",po::value<bool>(&opt_quick)->default_value(false)->zero_tokens(),
  "Repair a damaged Archive version 3 or above file.")
  ("set-auto-increment,A",po::value<uint64_t>(&new_auto_increment_value)->default_value(0),
  "Force auto_increment to start at this or higher value. If no value is given, then sets the next auto_increment value to the highest used value for the auto key + 1.")
  ("silent,s",po::value<bool>(&opt_silent)->default_value(false)->zero_tokens(),
  "Only print errors. One can use two -s to make archive_reader very silent.")
  ("tmpdir,t",po::value<string>(&opt_tmpdir)->default_value(""),
  "Path for temporary files.") 
  ("version,V","Print version and exit")
  ;

  unsigned int ret;
  azio_stream reader_handle;

  internal::my_init();
  MY_INIT(argv[0]);
  internal::load_defaults("drizzle", load_default_groups, &argc, &argv);
  default_argv= argv;
  
  po::variables_map vm;
  po::store(po::parse_command_line(argc,argv,long_options),vm);
  po::notify(vm);

  if(vm.count("force")||vm.count("quiet")||vm.count("tmpdir"))
    cout<<"Not implemented yet";

  if(vm.count("version"))
  {
    printf("%s  Ver %s, for %s-%s (%s)\n", internal::my_progname, SHOW_VERSION,
        HOST_VENDOR, HOST_OS, HOST_CPU);
    exit(0);
  }
  
  if(vm.count("set-auto-increment"))
  {
    opt_autoincrement= true;
  }

  if(vm.count("help")||argc == 0)
  {
    printf("%s  Ver %s, for %s-%s (%s)\n", internal::my_progname, SHOW_VERSION,
        HOST_VENDOR, HOST_OS, HOST_CPU);
    puts("This software comes with ABSOLUTELY NO WARRANTY. This is free software,\
       \nand you are welcome to modify and redistribute it under the GPL \
       license\n");
    puts("Read and modify Archive files directly\n");
    printf("Usage: %s [OPTIONS] file_to_be_looked_at [file_for_backup]\n", internal::my_progname);
    internal::print_defaults("drizzle", load_default_groups);
    cout<<long_options<<endl;    
    exit(0); 
  }
  
  if (argc < 1)
  {
    printf("No file specified. \n");
    return 0;
  }

  if (!(ret= azopen(&reader_handle, argv[0], O_RDONLY, AZ_METHOD_BLOCK)))
  {
    printf("Could not open Archive file\n");
    return 0;
  }

  if (opt_autoincrement)
  {
    azio_stream writer_handle;

    if (new_auto_increment_value)
    {
      if (reader_handle.auto_increment >= new_auto_increment_value)
      {
        printf("Value is lower then current value\n");
        goto end;
      }
    }
    else
    {
      new_auto_increment_value= reader_handle.auto_increment + 1;
    }

    if (!(ret= azopen(&writer_handle, argv[0], O_CREAT|O_RDWR,
                      AZ_METHOD_BLOCK)))
    {
      printf("Could not open file for update: %s\n", argv[0]);
      goto end;
    }

    writer_handle.auto_increment= new_auto_increment_value;

    azclose(&writer_handle);
    azflush(&reader_handle, Z_SYNC_FLUSH);
  }

  printf("Version %u\n", reader_handle.version);
  if (reader_handle.version > 2)
  {
    printf("\tMinor version %u\n", reader_handle.minor_version);
    printf("\tStart position %"PRIu64"\n", (uint64_t)reader_handle.start);
    printf("\tBlock size %u\n", reader_handle.block_size);
    printf("\tRows %"PRIu64"\n", reader_handle.rows);
    printf("\tAutoincrement %"PRIu64"\n", reader_handle.auto_increment);
    printf("\tCheck Point %"PRIu64"\n", reader_handle.check_point);
    printf("\tForced Flushes %"PRIu64"\n", reader_handle.forced_flushes);
    printf("\tLongest Row %u\n", reader_handle.longest_row);
    printf("\tShortest Row %u\n", reader_handle.shortest_row);
    printf("\tState %s\n", ( reader_handle.dirty ? "dirty" : "clean"));
    printf("\tTable protobuf message stored at %u\n",
           reader_handle.frm_start_pos);
    printf("\tComment stored at %u\n", reader_handle.comment_start_pos);
    printf("\tData starts at %u\n", (unsigned int)reader_handle.start);
    if (reader_handle.frm_start_pos)
      printf("\tTable proto message length %u\n", reader_handle.frm_length);
    if (reader_handle.comment_start_pos)
    {
      char *comment =
        (char *) malloc(sizeof(char) * reader_handle.comment_length);
      azread_comment(&reader_handle, comment);
      printf("\tComment length %u\n\t\t%.*s\n", reader_handle.comment_length,
             reader_handle.comment_length, comment);
      free(comment);
    }
  }
  else
  {
    goto end;
  }

  printf("\n");

  if (opt_check)
  {
    int error;
    unsigned int row_read;
    uint64_t row_count= 0;

    while ((row_read= azread_row(&reader_handle, &error)))
    {
      if (error == Z_STREAM_ERROR)
      {
        printf("Table is damaged\n");
        goto end;
      }

      row_count++;

      if (row_read > reader_handle.longest_row)
      {
        printf("Table is damaged, row %"PRIu64" is invalid\n", row_count);
        goto end;
      }
    }

    printf("Found %"PRIu64" rows\n", row_count);
  }

  if (opt_backup)
  {
    int error;
    unsigned int row_read;
    uint64_t row_count= 0;
    char *buffer;

    azio_stream writer_handle;

    buffer= (char *)malloc(reader_handle.longest_row);
    if (buffer == NULL)
    {
      printf("Could not allocate memory for row %"PRIu64"\n", row_count);
      goto end;
    }


    if (!(ret= azopen(&writer_handle, argv[1], O_CREAT|O_RDWR,
                      AZ_METHOD_BLOCK)))
    {
      printf("Could not open file for backup: %s\n", argv[1]);
      goto end;
    }

    writer_handle.auto_increment= reader_handle.auto_increment;
    if (reader_handle.frm_length)
    {
      char *ptr;
      ptr= (char *)malloc(sizeof(char) * reader_handle.frm_length);
      if (ptr == NULL)
      {
        printf("Could not allocate enough memory\n");
        goto end;
      }
      azread_frm(&reader_handle, ptr);
      azwrite_frm(&writer_handle, ptr, reader_handle.frm_length);
      free(ptr);
    }

    if (reader_handle.comment_length)
    {
      char *ptr;
      ptr= (char *)malloc(sizeof(char) * reader_handle.comment_length);
      azread_comment(&reader_handle, ptr);
      azwrite_comment(&writer_handle, ptr, reader_handle.comment_length);
      free(ptr);
    }

    while ((row_read= azread_row(&reader_handle, &error)))
    {
      if (error == Z_STREAM_ERROR || error)
      {
        printf("Table is damaged\n");
        goto end;
      }

      /* If we read nothing we are at the end of the file */
      if (row_read == 0)
        break;

      row_count++;

      azwrite_row(&writer_handle, reader_handle.row_ptr, row_read);

      if (reader_handle.rows == writer_handle.rows)
        break;
    }

    free(buffer);

    azclose(&writer_handle);
  }

  if (opt_extract_table_message)
  {
    int frm_file;
    char *ptr;
    frm_file= internal::my_open(argv[1], O_CREAT|O_RDWR, MYF(0));
    ptr= (char *)malloc(sizeof(char) * reader_handle.frm_length);
    if (ptr == NULL)
    {
      printf("Could not allocate enough memory\n");
      goto end;
    }
    azread_frm(&reader_handle, ptr);
    internal::my_write(frm_file, (unsigned char*) ptr, reader_handle.frm_length, MYF(0));
    internal::my_close(frm_file, MYF(0));
    free(ptr);
  }

end:
  printf("\n");
  azclose(&reader_handle);

  internal::my_end();

}
  catch(exception &e)
  {
    cerr<<"Error"<<e.what()<<endl;
  }
  return 0;
}

