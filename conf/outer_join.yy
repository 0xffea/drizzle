
query:
  { @nonaggregates = () ; $tables = 0 ; $fields = 0 ;  "" } query_type ;

query_type:
  simple_select | simple_select | mixed_select | mixed_select | mixed_select | aggregate_select ;

mixed_select:
        { $stack->push() } SELECT distinct straight_join select_option select_list FROM join WHERE where_list group_by_clause having_clause order_by_clause { $stack->pop(undef) } ;

simple_select:
        { $stack->push() } SELECT distinct straight_join select_option simple_select_list FROM join WHERE where_list  optional_group_by having_clause order_by_clause { $stack->pop(undef) } ;

aggregate_select:
        { $stack->push() } SELECT distinct straight_join select_option aggregate_select_list FROM join WHERE where_list optional_group_by having_clause order_by_clause { $stack->pop(undef) } ;

distinct: DISTINCT | | | |  ;

select_option:  | | | | | | | | | SQL_SMALL_RESULT ;

straight_join:  | | | | | | | | | | | STRAIGHT_JOIN ;

select_list:
	new_select_item |
	new_select_item , select_list |
        new_select_item , select_list ;

simple_select_list:
        nonaggregate_select_item |
        nonaggregate_select_item , simple_select_list |
        nonaggregate_select_item , simple_select_list ;

aggregate_select_list:
        aggregate_select_item | aggregate_select_item |
        aggregate_select_item, aggregate_select_list ;

new_select_item:
        nonaggregate_select_item |
        nonaggregate_select_item |        
        nonaggregate_select_item |
        nonaggregate_select_item |        
        nonaggregate_select_item |
	aggregate_select_item ;

nonaggregate_select_item:
        table_alias . int_field_name AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;

aggregate_select_item:
        aggregate table_alias . int_field_name ) AS {"field".++$fields } ; 
	
join:
       { $stack->push() }      
       table_or_join 
       { $stack->set("left",$stack->get("result")); }
       left_right outer JOIN table_or_join 
       ON 
       { my $left = $stack->get("left"); my %s=map{$_=>1} @$left; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . int_indexed = 
       { my $right = $stack->get("result"); my %s=map{$_=>1} @$right; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . int_indexed
       { my $left = $stack->get("left");  my $right = $stack->get("result"); my @n = (); push(@n,@$right); push(@n,@$left); $stack->pop(\@n); return undef } ;

where_list:
        where_item | where_item |
        ( where_list and_or where_item ) ;

where_item:
        existing_table_item . `pk` comparison_operator _digit  |
        existing_table_item . `pk` comparison_operator existing_table_item . int_field_name  |
	existing_table_item . int_field_name comparison_operator _digit  |
        existing_table_item . int_field_name comparison_operator existing_table_item . int_field_name |
        existing_table_item . int_field_name IS not NULL |
        existing_table_item . int_field_name not IN (number_list) |
        existing_table_item . int_field_name  not BETWEEN _digit[invariant] AND ( _digit[invariant] + _digit );

number_list:
        _digit | number_list, _digit ;

################################################################################
# We ensure that a GROUP BY statement includes all nonaggregates.              #
# This helps to ensure the query is more useful in detecting real errors /     #
# that the query doesn't lend itself to variable result sets                   #
################################################################################
group_by_clause:
	{ scalar(@nonaggregates) > 0 ? " GROUP BY ".join (', ' , @nonaggregates ) : "" }  ;

optional_group_by:
        | | | | | | | | group_by_clause ;

having_clause:
	| HAVING having_list;

having_list:
        having_item |
        having_item |
	(having_list and_or having_item)  ;

having_item:
	existing_select_item comparison_operator _digit ;

################################################################################
# We use the total_order_by rule when using the LIMIT operator to ensure that  #
# we have a consistent result set - server1 and server2 should not differ      #
################################################################################

order_by_clause:
	|
        ORDER BY total_order_by desc limit |
	ORDER BY order_by_list ;

total_order_by:
	{ join(', ', map { "field".$_ } (1..$fields) ) };

order_by_list:
	order_by_item  |
	order_by_item  , order_by_list ;

order_by_item:
	existing_select_item desc ;

desc:
        ASC | | | | | DESC ; 

################################################################################
# We mix digit and _digit here.  We want to alter the possible values of LIMIT #
# To ensure we hit varying EXPLAIN plans, but the OFFSET can be smaller        #
################################################################################

limit:
	| | LIMIT limit_size | LIMIT limit_size OFFSET _digit;

table_or_join:
           table | table | table | table | table | table | 
           table | table | table | table | join | join ;

table_disabled:
# We use the "AS table" bit here so we can have unique aliases if we use the same table many times
       { $stack->push(); my $x = $prng->arrayElement(\@table_set)." AS table".++$tables;  my @s=($x); $stack->pop(\@s); $x } ;


table:
# We use the "AS table" bit here so we can have unique aliases if we use the same table many times
       { $stack->push(); my $x = $prng->arrayElement($executors->[0]->tables())." AS table".++$tables;  my @s=($x); $stack->pop(\@s); $x } ;

int_field_name:
  `pk` | `int_key` | `int` ;

int_indexed:
   `pk` | `int_key` ;

table_alias:
  table1 | table1 | table1 | table1 | table1 | table1 | table1 | table1 | table1 | table1 |
  table2 | table2 | table2 | table2 | table2 | table2 | table2 | table2 | table2 | other_table ;

other_table:
  table3 | table3 | table3 | table3 | table3 | table4 | table4 | table5 ;

existing_table_item:
	{ "table".$prng->int(1,$tables) };

existing_select_item:
	{ "field".$prng->int(1,$fields) };

_digit:
    1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
    1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | _tinyint_unsigned ;
 

and_or:
   AND | AND | OR ;

comparison_operator:
	= | > | < | != | <> | <= | >= ;

aggregate:
	COUNT( | SUM( | MIN( | MAX( ;

not:
	| | | NOT;

left_right:
	LEFT | LEFT | LEFT | RIGHT ;

outer:
	| | | | OUTER ;

################################################################################
# We define LIMIT_rows in this fashion as LIMIT values can differ depending on      #
# how large the LIMIT is - LIMIT 2 = LIMIT 9 != LIMIT 19                       #
################################################################################

limit_size:
    1 | 2 | 10 | 100 | 1000;

