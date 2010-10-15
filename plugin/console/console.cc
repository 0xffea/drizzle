/* Copyright (C) 2009 Sun Microsystems

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#include "config.h"
#include <drizzled/gettext.h>
#include <drizzled/plugin/listen_tcp.h>
#include <drizzled/plugin/client.h>
#include <drizzled/session.h>
#include <drizzled/module/option_map.h>

#include <iostream>

#include <boost/program_options.hpp>

using namespace std;
using namespace drizzled;

namespace po= boost::program_options;

static bool debug_enabled= false;
static char* username= NULL;
static char* password= NULL;
static char* db= NULL;


class ClientConsole: public plugin::Client
{
  bool is_dead;
  uint32_t column;
  uint32_t max_column;

public:
  ClientConsole():
    is_dead(false),
    column(0),
    max_column(0)
  {}

  virtual void printDebug(const char *message)
  {
    if (debug_enabled)
      cout << "CONSOLE: " << message << endl;
  }

  virtual int getFileDescriptor(void)
  {
    printDebug("getFileDescriptor");
    return 0;
  }

  virtual bool isConnected(void)
  {
    printDebug("isConnected");
    return true;
  }

  virtual bool isReading(void)
  {
    printDebug("isReading");
    return false;
  }

  virtual bool isWriting(void)
  {
    printDebug("isWriting");
    return false;
  }

  virtual bool flush(void)
  {
    printDebug("flush");
    return false;
  }

  virtual void close(void)
  {
    printDebug("close");
    is_dead= true;
  }

  virtual bool authenticate(void)
  {
    printDebug("authenticate");
    session->getSecurityContext().setUser(username);
    return session->checkUser(password, strlen(password), db);
  }

  virtual bool readCommand(char **packet, uint32_t *packet_length)
  {
    uint32_t length;

    if (is_dead)
      return false;

    cout << "drizzled> ";

    length= 1024;
    *packet= NULL;

    /* Start with 1 byte offset so we can set command. */
    *packet_length= 1;

    do
    {
      *packet= (char *)realloc(*packet, length);
      if (*packet == NULL)
        return false;

      cin.clear();
      cin.getline(*packet + *packet_length, length - *packet_length, ';');
      *packet_length+= cin.gcount();
      length*= 2;
    }
    while (cin.eof() == false && cin.fail() == true);

    if ((*packet_length == 1 && cin.eof() == true) ||
        !strncasecmp(*packet + 1, "quit", 4) ||
        !strncasecmp(*packet + 1, "exit", 4))
    {
      is_dead= true;
      *packet_length= 1;
      (*packet)[0]= COM_SHUTDOWN;
      return true;
    }

    /* Skip \r and \n for next time. */
    cin.ignore(2, '\n');

    (*packet)[0]= COM_QUERY;
    return true;
  }

  virtual void sendOK(void)
  {
    cout << "OK" << endl;
  }

  virtual void sendEOF(void)
  {
    printDebug("sendEOF");
  }

  virtual void sendError(uint32_t sql_errno, const char *err)
  {
    cout << "Error: " << sql_errno << " " << err << endl;
  }

  virtual bool sendFields(List<Item> *list)
  {
    List_iterator_fast<Item> it(*list);
    Item *item;

    column= 0;
    max_column= 0;

    while ((item=it++))
    {
      SendField field;
      item->make_field(&field);
      cout << field.col_name << "\t";
      max_column++;
    }

    cout << endl;

    return false;
  }

  virtual void checkRowEnd(void)
  {
    if (++column % max_column == 0)
      cout << endl;
  }

  using Client::store;

  virtual bool store(Field *from)
  {
    if (from->is_null())
      return store();

    char buff[MAX_FIELD_WIDTH];
    String str(buff, sizeof(buff), &my_charset_bin);
    from->val_str(&str);
    return store(str.ptr(), str.length());
  }

  virtual bool store(void)
  {
    cout << "NULL" << "\t";
    checkRowEnd();
    return false;
  }

  virtual bool store(int32_t from)
  {
    cout << from << "\t";
    checkRowEnd();
    return false;
  }

  virtual bool store(uint32_t from)
  {
    cout << from << "\t";
    checkRowEnd();
    return false;
  }

  virtual bool store(int64_t from)
  {
    cout << from << "\t";
    checkRowEnd();
    return false;
  }

  virtual bool store(uint64_t from)
  {
    cout << from << "\t";
    checkRowEnd();
    return false;
  }

  virtual bool store(double from, uint32_t decimals, String *buffer)
  {
    buffer->set_real(from, decimals, &my_charset_bin);
    return store(buffer->ptr(), buffer->length());
  }

  virtual bool store(const char *from, size_t length)
  {
    cout.write(from, length);
    cout << "\t";
    checkRowEnd();
    return false;
  }

  virtual bool haveMoreData(void)
  {
    printDebug("haveMoreData");
    return false;
  }

  virtual bool haveError(void)
  {
    printDebug("haveError");
    return false;
  }

  virtual bool wasAborted(void)
  {
    printDebug("wasAborted");
    return false;
  }
};

class ListenConsole: public plugin::Listen
{
  int pipe_fds[2];

public:
  ListenConsole(const std::string &name_arg) :
    plugin::Listen(name_arg)
  {
    pipe_fds[0]= -1;
  }

  virtual ~ListenConsole()
  {
    if (pipe_fds[0] != -1)
    {
      close(pipe_fds[0]);
      close(pipe_fds[1]);
    }

    /* Cleanup from the module strdup'ing these below */
    free(username);
    free(password);
    free(db);
  }

  virtual bool getFileDescriptors(std::vector<int> &fds)
  {
    if (debug_enabled)
      enabled= true;

    if (pipe(pipe_fds) == -1)
    {
      errmsg_printf(ERRMSG_LVL_ERROR, _("pipe() failed with errno %d"), errno);
      return true;
    }

    fds.push_back(pipe_fds[0]);
    assert(write(pipe_fds[1], "\0", 1) == 1);
    return false;
  }

  virtual drizzled::plugin::Client *getClient(int fd)
  {
    char buffer[1];
    assert(read(fd, buffer, 1) == 1);
    return new ClientConsole;
  }
};

static int init(drizzled::module::Context &context)
{
  const module::option_map &vm= context.getOptions();
  /* duplicating these here means they need to be freed. They're global, so
     we'll just have the ListenConsole object do it in its destructor */
  if (vm.count("username"))
    username= strdup(vm["username"].as<string>().c_str());
  else
    username= strdup("");

  if (vm.count("password"))
    password= strdup(vm["password"].as<string>().c_str());
  else
    password= strdup("");

  if (vm.count("db"))
    db= strdup(vm["db"].as<string>().c_str());
  else
    db= strdup("");

  context.add(new ListenConsole("console"));
  context.registerVariable(new sys_var_bool_ptr("enable", &enabled));
  context.registerVariable(new sys_var_bool_ptr("debug_enable", &debug_enabled));
  context.registerVariable(new sys_var_const_str("username", username));
  context.registerVariable(new sys_var_const_str("password", password));
  context.registerVariable(new sys_var_const_str("db", db));
  return 0;
}

static void init_options(drizzled::module::option_context &context)
{
  context("enable",
          po::value<bool>(&enabled)->default_value(false)->zero_tokens(),
          N_("Enable the console."));
  context("debug",
          po::value<bool>(&debug_enabled)->default_value(false)->zero_tokens(),
          N_("Turn on extra debugging."));
  context("username",
          po::value<string>(),
          N_("User to use for auth."));
  context("password",
          po::value<string>(),
          N_("Password to use for auth."));
  context("db",
          po::value<string>(),
          N_("Default database to use."));
}

DRIZZLE_DECLARE_PLUGIN
{
  DRIZZLE_VERSION_ID,
  "console",
  "0.1",
  "Eric Day",
  "Console Client",
  PLUGIN_LICENSE_BSD,
  init,   /* Plugin Init */
  NULL,   /* system variables */
  init_options    /* config options */
}
DRIZZLE_DECLARE_PLUGIN_END;
