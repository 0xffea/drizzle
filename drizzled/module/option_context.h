/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Monty Taylor
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

/**
 * @file
 * @brief An Proxy Wrapper around options_description_easy_init
 */

#ifndef DRIZZLED_MODULE_OPTION_CONTEXT_H
#define DRIZZLED_MODULE_OPTION_CONTEXT_H

#include "drizzled/visibility.h"

#include <boost/program_options.hpp>

#include <string>

namespace drizzled
{
namespace module
{

namespace po=boost::program_options;

/**
 * Options proxy wrapper. Provides pre-pending of module name to each option
 * which is added.
 */
class DRIZZLED_API option_context
{
  const std::string &module_name;
  po::options_description_easy_init po_options;

public:

  option_context(const std::string &module_name_in,
                 po::options_description_easy_init po_options_in);

  option_context& operator()(const char* name,
                             const char* description);

  option_context& operator()(const char* name,
                             const po::value_semantic* s);

  option_context& operator()(const char* name,
                             const po::value_semantic* s,
                             const char* description);

  static std::string prepend_name(std::string module_name_in,
                                  const char *name);

private:
  
  /**
   * Don't allow default construction.
   */
  option_context();

  /**
   * Don't allow copying of objects.
   */
  option_context(const option_context &);

  /**
   * Don't allow assignment of objects.
   */
  option_context& operator=(const option_context &);
};

} /* namespace module */
} /* namespace drizzled */


#endif /* DRIZZLED_MODULE_OPTION_CONTEXT_H */
