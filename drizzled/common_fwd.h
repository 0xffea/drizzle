#pragma once

namespace drizzled {

namespace internal
{
  typedef struct st_io_cache IO_CACHE;
  
  struct st_my_thread_var;
}

namespace identifier 
{ 
  class Schema; 
}

namespace item
{
  class Boolean;
  class False;
  class True;
}

namespace memory
{
  class Root;
}

namespace message
{
  class Resultset;
  class Schema;
  class Statement;
  class Transaction;
}

namespace module
{
  class Registry;
}

namespace plugin 
{ 
  class Client;
  class EventObserverList;
  class MonitoredInTransaction;
  class Scheduler;
  class StorageEngine;
  class TransactionApplier;
  class TransactionReplicator;
  class TransactionalStorageEngine;
  class XaResourceManager;
}

namespace optimizer 
{ 
  class CostVector; 
  class Position;
  class SqlSelect;
}

namespace table 
{ 
  class Placeholder; 
}

namespace type 
{ 
  class Decimal;
  class Time; 
}

typedef class Item COND;
typedef struct charset_info_st CHARSET_INFO;
typedef struct my_locale_st MY_LOCALE;
typedef struct system_status_var system_status_var;
typedef struct st_typelib TYPELIB;

class AlterColumn;
class AlterDrop;
class AlterInfo;
class Arg_comparator;
class CachedDirectory;
class CopyField;
class Create_func;
class CreateField;
class Date;
class DateTime;
class DrizzleLock;
class DrizzleXid;
class Field;
class Field_blob;
class ForeignKeyInfo;
class Hybrid_type;
class Hybrid_type_traits;
class Internal_error_handler;
class Item;
class Item_bool_func2;
class Item_equal;
class Item_field;
class Item_ident;
class Item_in_subselect;
class Item_row;
class Item_sum;
class Item_sum_hybrid;
class Join;
class JoinTable;
class LEX;
class Lex_input_stream;
class Name_resolution_context;
class SecurityContext;
class Select_Lex;
class Select_Lex_Unit;
class select_result;
class SendField;
class Session;
class SortField;
class SortParam;
class String;
class sys_var_str;
class Table;
class Table_ident;
class TableList;
class TableShare;
class TableShareInstance;
class Time;
class Time_zone;
// class TYPELIB;
class user_var_entry;
class var;
struct CacheField;
struct option;
struct Order;

} // namespace drizzled
