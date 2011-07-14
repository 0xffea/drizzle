/*
 * Drizzle Client & Protocol Library
 *
 * Copyright (C) 2008 Eric Day (eday@oddments.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 *     * The names of its contributors may not be used to endorse or
 * promote products derived from this software without specific prior
 * written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <iostream>
#include <libdrizzle/libdrizzle.hpp>
#include <netdb.h>
#include <unistd.h>

using namespace std;

int main(int argc, char *argv[])
{
  const char* db= "information_schema";
  const char* host= NULL;
  bool mysql= false;
  in_port_t port= 0;
  const char* query= "select table_schema, table_name from tables";
  drizzle_verbose_t verbose= DRIZZLE_VERBOSE_NEVER;

  for (int c; (c = getopt(argc, argv, "d:h:mp:q:v")) != -1; )
  {
    switch (c)
    {
    case 'd':
      db= optarg;
      break;

    case 'h':
      host= optarg;
      break;

    case 'm':
      mysql= true;
      break;

    case 'p':
      port= (in_port_t)atoi(optarg);
      break;

    case 'q':
      query= optarg;
      break;

    case 'v':
      switch (verbose)
      {
      case DRIZZLE_VERBOSE_NEVER:
        verbose= DRIZZLE_VERBOSE_FATAL;
        break;
      case DRIZZLE_VERBOSE_FATAL:
        verbose= DRIZZLE_VERBOSE_ERROR;
        break;
      case DRIZZLE_VERBOSE_ERROR:
        verbose= DRIZZLE_VERBOSE_INFO;
        break;
      case DRIZZLE_VERBOSE_INFO:
        verbose= DRIZZLE_VERBOSE_DEBUG;
        break;
      case DRIZZLE_VERBOSE_DEBUG:
        verbose= DRIZZLE_VERBOSE_CRAZY;
        break;
      case DRIZZLE_VERBOSE_CRAZY:
      case DRIZZLE_VERBOSE_MAX:
        break;
      }
      break;

    default:
      cout << 
        "usage: " << argv[0] << " [-d <db>] [-h <host>] [-m] [-p <port>] [-q <query>] [-v]\n"
        "\t-d <db>    - Database to use for query\n"
        "\t-h <host>  - Host to connect to\n"
        "\t-m         - Use the MySQL protocol\n"
        "\t-p <port>  - Port to connect to\n"
        "\t-q <query> - Query to run\n"
        "\t-v         - Increase verbosity level\n";
      return 1;
    }
  }

  drizzle::drizzle_c drizzle;
  drizzle_set_verbose(&drizzle.b_, verbose);
  drizzle::connection_c* con= new drizzle::connection_c(drizzle);
  if (mysql)
    drizzle_con_add_options(&con->b_, DRIZZLE_CON_MYSQL);
  drizzle_con_set_tcp(&con->b_, host, port);
  drizzle_con_set_db(&con->b_, db);
  drizzle::result_c result;
  if (con->query(result, query))
  {
    cerr << "drizzle_query: " << con->error() << endl;
    return 1;
  }
  while (drizzle_row_t row= result.row_next())
  {
    for (int x= 0; x < result.column_count(); x++)
    {
      if (x)
        cout << ", ";
      cout << (row[x] ? row[x] : "NULL");
    }
    cout << endl;
  }
  return 0;
}
