/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
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

#include <drizzled/server_includes.h>
#include "drizzled/slot/listen.h"
#include "drizzled/plugin/registry.h"
#include <drizzled/gettext.h>
#include <drizzled/error.h>
#include <drizzled/plugin/listen.h>
#include <drizzled/plugin/client.h>

#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>

using namespace std;
using namespace drizzled;

slot::Listen::Listen():
  fd_list(NULL),
  fd_count(0)
{
}

slot::Listen::~Listen()
{
  if (fd_list != NULL)
    free(fd_list);
}

void slot::Listen::add(const plugin::Listen &listen_obj)
{
  listen_list.push_back(&listen_obj);
}

void slot::Listen::remove(const plugin::Listen &listen_obj)
{
  listen_list.erase(::std::remove(listen_list.begin(),
                                  listen_list.end(),
                                  &listen_obj),
                    listen_list.end());
}

bool slot::Listen::bindAll(const char *host, uint32_t bind_timeout)
{
  vector<const plugin::Listen *>::iterator it;
  int ret;
  char host_buf[NI_MAXHOST];
  char port_buf[NI_MAXSERV];
  struct addrinfo hints;
  struct addrinfo *ai;
  struct addrinfo *ai_list;
  int fd= -1;
  uint32_t waited;
  uint32_t this_wait;
  uint32_t retry;
  struct linger ling= {0, 0};
  int flags= 1;
  struct pollfd *tmp_fd_list;

  for (it= listen_list.begin(); it < listen_list.end(); ++it)
  {
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags= AI_PASSIVE;
    hints.ai_socktype= SOCK_STREAM;

    snprintf(port_buf, NI_MAXSERV, "%d", (*it)->getPort());
    ret= getaddrinfo(host, port_buf, &hints, &ai_list);
    if (ret != 0)
    {
      errmsg_printf(ERRMSG_LVL_ERROR, _("getaddrinfo() failed with error %s"),
                    gai_strerror(ret));
      return true;
    }

    for (ai= ai_list; ai != NULL; ai= ai->ai_next)
    {
      ret= getnameinfo(ai->ai_addr, ai->ai_addrlen, host_buf, NI_MAXHOST,
                       port_buf, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);
      if (ret != 0)
      { 
        strcpy(host_buf, "-");
        strcpy(port_buf, "-");
      }

      fd= socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
      if (fd == -1)
      {
        /*
          Call to socket() can fail for some getaddrinfo results, try another.
        */
        continue;
      }

#ifdef IPV6_V6ONLY
      if (ai->ai_family == AF_INET6)
      {
        flags= 1;
        ret= setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &flags, sizeof(flags));
        if (ret != 0)
        {
          errmsg_printf(ERRMSG_LVL_ERROR,
                        _("setsockopt(IPV6_V6ONLY) failed with errno %d"),
                        errno);
          return true;
        }
      }
#endif

      ret= setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags));
      if (ret != 0)
      {
        errmsg_printf(ERRMSG_LVL_ERROR,
                      _("setsockopt(SO_REUSEADDR) failed with errno %d"),
                      errno);
        return true;
      }

      ret= setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags));
      if (ret != 0)
      {
        errmsg_printf(ERRMSG_LVL_ERROR,
                      _("setsockopt(SO_KEEPALIVE) failed with errno %d"),
                      errno);
        return true;
      }

      ret= setsockopt(fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
      if (ret != 0)
      {
        errmsg_printf(ERRMSG_LVL_ERROR,
                      _("setsockopt(SO_LINGER) failed with errno %d"),
                      errno);
        return true;
      }

      ret= setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flags, sizeof(flags));
      if (ret != 0)
      {
        errmsg_printf(ERRMSG_LVL_ERROR,
                      _("setsockopt(TCP_NODELAY) failed with errno %d"),
                      errno);
        return true;
      }

      /*
        Sometimes the port is not released fast enough when stopping and
        restarting the server. This happens quite often with the test suite
        on busy Linux systems. Retry to bind the address at these intervals:
        Sleep intervals: 1, 2, 4,  6,  9, 13, 17, 22, ...
        Retry at second: 1, 3, 7, 13, 22, 35, 52, 74, ...
        Limit the sequence by bind_timeout.
      */
      for (waited= 0, retry= 1; ; retry++, waited+= this_wait)
      {
        if (((ret= ::bind(fd, ai->ai_addr, ai->ai_addrlen)) == 0) ||
            (errno != EADDRINUSE) || (waited >= bind_timeout))
        {
          break;
        }

        errmsg_printf(ERRMSG_LVL_INFO, _("Retrying bind() on %u"),
                      (*it)->getPort());
        this_wait= retry * retry / 3 + 1;
        sleep(this_wait);
      }

      if (ret < 0)
      {
        errmsg_printf(ERRMSG_LVL_ERROR, _("bind() failed with errno: %d"),
                      errno);
        errmsg_printf(ERRMSG_LVL_ERROR,
                      _("Do you already have another drizzled running?"));
        return true;
      }

      if (listen(fd, (int) back_log) < 0)
      {
        errmsg_printf(ERRMSG_LVL_ERROR,
                      _("listen() failed with errno %d"), errno);
        return true;
      }

      tmp_fd_list= (struct pollfd *)realloc(fd_list,
                                        sizeof(struct pollfd) * (fd_count + 1));
      if (tmp_fd_list == NULL)
      {
        errmsg_printf(ERRMSG_LVL_ERROR, _("realloc() failed with errno %d"),
                      errno);
        return true;
      }

      fd_list= tmp_fd_list;
      fd_list[fd_count].fd= fd;
      fd_list[fd_count].events= POLLIN | POLLERR;
      listen_fd_list.push_back(*it);
      fd_count++;

      errmsg_printf(ERRMSG_LVL_INFO, _("Listening on %s:%s\n"), host_buf,
                    port_buf);
    }
  }

  if (fd_count == 0)
  {
    errmsg_printf(ERRMSG_LVL_ERROR,
                  _("No sockets could be bound for listening"));
    return true;
  }

  freeaddrinfo(ai_list);

  /*
    We need a pipe to wakeup the listening thread since some operating systems
    are stupid. *cough* OSX *cough*
  */
  if (pipe(wakeup_pipe) == -1)
  {
    errmsg_printf(ERRMSG_LVL_ERROR, _("pipe() failed with errno %d"), errno);
    return true;
  }

  tmp_fd_list= (struct pollfd *)realloc(fd_list,
                                        sizeof(struct pollfd) * (fd_count + 1));
  if (tmp_fd_list == NULL)
  {
    errmsg_printf(ERRMSG_LVL_ERROR, _("realloc() failed with errno %d"), errno);
    return true;
  }

  fd_list= tmp_fd_list;
  fd_list[fd_count].fd= wakeup_pipe[0];
  fd_list[fd_count].events= POLLIN | POLLERR;
  fd_count++;

  return false;
}

plugin::Client *slot::Listen::getClient(void) const
{
  int ready;
  uint32_t x;
  uint32_t retry;
  int fd;
  plugin::Client *client;
  uint32_t error_count= 0;

  while (1)
  {
    ready= poll(fd_list, fd_count, -1);
    if (ready == -1)
    {
      if (errno != EINTR)
      {
        errmsg_printf(ERRMSG_LVL_ERROR, _("poll() failed with errno %d"),
                      errno);
      }

      continue;
    }
    else if (ready == 0)
      continue;

    for (x= 0; x < fd_count; x++)
    {
      if (fd_list[x].revents != POLLIN)
        continue;

      /* Check to see if the wakeup_pipe was written to. */
      if (x == fd_count - 1)
      {
        /* Close all file descriptors now. */
        for (x= 0; x < fd_count; x++)
        {
          (void) ::shutdown(fd_list[x].fd, SHUT_RDWR);
          (void) close(fd_list[x].fd);
          fd_list[x].fd= -1;
        }

        /* wakeup_pipe[0] was closed in the for loop above. */
        (void) close(wakeup_pipe[1]);

        return NULL;
      }

      for (retry= 0; retry < MAX_ACCEPT_RETRY; retry++)
      {
        fd= accept(fd_list[x].fd, NULL, 0);
        if (fd != -1 || (errno != EINTR && errno != EAGAIN))
          break;
      }

      if (fd == -1)
      {
        if ((error_count++ & 255) == 0)
        {
          errmsg_printf(ERRMSG_LVL_ERROR, _("accept() failed with errno %d"),
                        errno);
        }

        if (errno == ENFILE || errno == EMFILE)
          sleep(1);

        continue;
      }

      if (!(client= listen_fd_list[x]->clientFactory()))
      {
        (void) ::shutdown(fd, SHUT_RDWR);
        (void) close(fd);
        continue;
      }

      if (client->setFileDescriptor(fd))
      {
        (void) ::shutdown(fd, SHUT_RDWR);
        (void) close(fd);
        delete client;
        continue;
      }

      return client;
    }
  }
}

plugin::Client *slot::Listen::getTmpClient(void) const
{
  assert(listen_list.size() > 0);
  return listen_list[0]->clientFactory();
}

void slot::Listen::shutdown(void)
{
  ssize_t ret= write(wakeup_pipe[1], "\0", 1);
  assert(ret == 1);
}
