/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Tim Penhey <tim@penhey.net>
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
#include <drizzled/plugin/function.h>
#include <drizzled/item/func.h>
#include <drizzled/function/str/strfunc.h>

#include <string>
#include <sstream>
#include <iostream>

using namespace std;

namespace plugin
{
  namespace rot13
  {
    char const* name= "rot13";

    namespace
    {
      std::string rot13(std::string const& s)
      {
        std::ostringstream sout;
        for (std::size_t i= 0, max= s.length(); i < max; ++i)
        {
          const char& c= s[i];
          if ((c >= 'a' && c <= 'm') || (c >= 'A' && c <= 'M'))
            sout << char(c + 13);
          else if ((c >= 'n' && c <= 'z') || (c >= 'N' && c <= 'Z'))
            sout << char(c - 13);
          else
            sout << c;
        }
        return sout.str();
      }
    }
    class Function : public Item_str_func
    {
    public:
      Function() : Item_str_func() {}
      const char *func_name() const { return name; }

      String *val_str(String *s)
      {
        String val;
        String *other= args[0]->val_str(&val);
        std::string to_rot= String_to_std_string(*other);
        return set_String_from_std_string(s, rot13(to_rot));
      };

      void fix_length_and_dec()
      {
        max_length= args[0]->max_length;
      }

      bool check_argument_count(int n)
      {
        return (n == 1);
      }
    };

    using drizzled::plugin::Create_function;
    using drizzled::plugin::Registry;
    typedef Create_function<Function> PluginFunction;
    PluginFunction *rot13_func= NULL;

    static int init(Registry &registry)
    {
      rot13_func= new PluginFunction(name);
      registry.add(rot13_func);
      return 0;
    }

    static int deinit(Registry &registry)
    {
      registry.remove(rot13_func);
      delete rot13_func;
      return 0;
    }
  }
}

DRIZZLE_PLUGIN(plugin::rot13::init, plugin::rot13::deinit, NULL, NULL);
