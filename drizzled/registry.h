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

#ifndef DRIZZLED_REGISTRY_H
#define DRIZZLED_REGISTRY_H

#include <map>
#include <set>
#include <string>
#include <algorithm>
#include <functional>

namespace drizzled {

namespace internal {

template<class T>
struct RegistryMapCompare
{
  bool operator() (const T& a, const T& b) const
  {
    return a.getName() < b.getName();
  }
};

template<class T>
struct RegistryMapCompare<T *>
{
  bool operator() (const T* a, const T* b) const
  {
    return a->getName() < b->getName();
  }
};

} /* namespace internal */

template<class T>
class Registry
{
  std::map<std::string, T> item_map;
  std::set<T,internal::RegistryMapCompare<T> > item_set;

  bool addItemEntry(std::string name, T item)
  {
    if (item_map.count(name) == 0)
    {
      item_map[name]= item;
      return false;
    }
    return true;
  }

  bool addItem(std::string name, T item)
  {

    /* First, add with no transform */
    if (addItemEntry(name, item))
      return true;

    /* Transform to lower, then add */ 
    transform(name.begin(), name.end(),
              name.begin(), ::tolower);

    /* Ignore failures here - the original name could be all lower */
    addItemEntry(name, item);

    /* Transform to upper, then add */ 
    transform(name.begin(), name.end(),
              name.begin(), ::toupper);

    /* Ignore failures here - the original name could be all upper */
    addItemEntry(name, item);

    return false;
  }

  void removeItem(std::string name)
  {

    /* First, remove with no transform */
    item_map.erase(name);

    /* Transform to lower, then remove */ 
    transform(name.begin(), name.end(),
              name.begin(), ::tolower);
    item_map.erase(name);
  }

public:

  typedef typename std::set<T>::const_iterator const_iterator;
  typedef typename std::set<T>::iterator iterator;
  typedef size_t size_type;

  T find(const char *name, size_t length)
  {
    std::string find_str(name, length);
    return find(find_str);
  }

  T find(const std::string &name)
  {

    typename std::map<std::string, T>::iterator find_iter;
    find_iter=  item_map.find(name);
    if (find_iter != item_map.end())
      return (*find_iter).second;
    
    /* We must look for lower case, so we make a copy of the input name */
    std::string lower_name(name);
    transform(lower_name.begin(), lower_name.end(),
              lower_name.begin(), ::tolower);
    find_iter=  item_map.find(lower_name);
    if (find_iter != item_map.end())
      return (*find_iter).second;

    return NULL;
  }

  /**
   * True == failure
   */
  bool add(T item)
  {
    bool failed= false;

    if (item_set.insert(item).second == false)
      return true;

    if (addItem(item->getName(), item))
      failed= true;

    const std::vector<std::string>& aliases= item->getAliases();
    if (!(failed) && (aliases.size() > 0))
    {
      typename std::vector<std::string>::const_iterator iter= aliases.begin();
      while (iter != aliases.end())
      {
        if(addItem(*iter, item))
          failed= true;
        ++iter;
      }
    }
   
    if (failed == true)
      remove(item);
    return failed; 
  }

  /**
   * Remove an item from the registry. We don't care about failure
   */
  void remove(T item)
  {
    removeItem(item->getName());

    const std::vector<std::string>& aliases= item->getAliases();
    if (aliases.size() > 0)
    {
      std::vector<std::string>::const_iterator iter= aliases.begin();
      while (iter != aliases.end())
      {
        removeItem(*iter);
        ++iter;
      }
    }
  }

  const_iterator begin()
  {
    return item_set.begin();
  }

  const_iterator end()
  {
    return item_set.end();
  }

  size_type count(std::string name) const
  {
    /* Transform to lower, then add */
    transform(name.begin(), name.end(),
              name.begin(), ::tolower);
    return item_map.count(name);
  }

  size_type size() const
  {
    return item_set.size();
  }
};

} /* namespace drizzled */

#endif /* DRIZZLED_REGISTRY_H */

