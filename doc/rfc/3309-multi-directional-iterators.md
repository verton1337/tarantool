# Multi-directional iterators

* **Status**: In progress
* **Start date**: 22-01-2020
* **Authors**: Nikita Pettik @korablev77 korablev@tarantool.org
* **Issues**: [#3243](https://github.com/tarantool/tarantool/issues/3243)


## Background and motivation

This RFC touches only Memtx engine and TREE index type (as the only available
in SQL and most common in user's practice). Multi-directional iterator is
an iterator which allows iterating through different key parts orders.
For instance: consider index `i` consisting of three key parts: `a`, `b` and `c`.
Creating and using casual iterator looks like:
```
i = box.space.T.index.i
i:select({1, 2, 3}, {iterator = 'EQ'}) -- returns all tuples which has
                                       -- fields a == 1, b == 2, c == 3.
```
It is OK to omit one or more key parts declaring key value to be searched. In
this case they are assumed to be nils:
`i:select({1}, {iterator = 'EQ'})` is the same as
`i:select({1, nil, nil}, {iterator = 'EQ'})`. So all tuples which has `a == 1`
are getting to the result set. More formally matching rule is following:
```
if (search-key-part[i] is nil)
{
  if (iterator is LT or GT) return FALSE
  return TRUE
}
```

Another example:
`i:select({1, 1, 1}, {iterator = 'GE'})`

Here all tuples with `a >= 1`, `b >= 1` and `c >= 1` are returned. But some users
may want to select tuples with `a >= 1`, `b >= 1` but `c < 1`. Or, alternatively,
somebody may be willing to get tuples ordered by `a` and `b` in ascending order
but by `c` in descending order: `i:select({}, {iterator = {'GE', 'GE', 'LE'})`.
It is analogue of common SQL query `SELECT * FROM t ORDER BY a ASC, b ASC, c DESC`.
These requests are obviously impossible to fulfill with current indexes and
iterators implementations. This RFC suggests ways to resolve mentioned problem
in particular for memtx TREE indexes.

## Implementation details

TREE indexes in memtx engine are implemented as BPS-trees (see
`src/lib/salad/bps_tree.h` for details). Keys correspond to particular values
of key parts; data - to pointers to tuples. Hence, all data
are sorted by their key values due to tree structure. For this reason HASH
indexes have only GT and EQ (and ergo GE) iterators - data stored in a hash is
unordered. Tree interface itself provides several functions to operate on data. 
Iteration process starts in `tree_iterator_start()` (which is called once as
`iterator->next()`): depending on its type iterator is positioned to the lower
or upper bound (via `memtx_tree_lower_bound()`) of range of values satisfying
search condition. In case key is not specified (i.e. empty), iterator is simply
set to the first or last element of tree. At this moment first element to be
returned (if any) is ready. To continue iterating `next` method of iterator
object is changed to one of `tree_iterator_next()`, `tree_iterator_prev()` or
their analogues for GE and LE iterators. Actually these functions fetch next
element from B-tree leaf block. If iterator points to the last element in the
block, it is set to the first element of the next block (leaf blocks are linked
into list); if there's no more blocks, iterator is invalidated and iteration
process is finished.  
Taking into account this information let's review several approaches how to
implement multi-directional iterators.

### Solution №1

First solution doesn't involve any additional data structures so that it deals
with multi-directional iterators only using existing B-tree structure.  
It fact, first key part can be used to locate first element as a candidate
in the range to be selected. To illustrate this point let's consider following
example:

```
s:create_index('i', {parts = {{1, 'integer'}, {2, 'integer'}}})`
s:insert({1, 0})
s:insert({1, 0})
s:insert({1, 1})
s:insert({2, 0})
s:insert({2, 1})
i:select({}, {iterator = {'GE', 'LE'}})
```

Result should be:
```
[1, 1]
[1, 0]
[1, 0]
[2, 1]
[2, 0]
```
Note that in case of casual GE iterator (i.e. {GE, GE} in terms of
multi-directional iterators) result is:
```
[1, 0]
[1, 0]
[1, 1]
[2, 0]
[2, 1]
```
As one can see, results are sorted in different orders by second key part,
but in the same order by first key part (not surprisingly). Assume first
element with first key part satisfying search condition is located: {1, 0}.
Then let's find out the first key part with different iterating order (in our
case it is second key part). Since order is different for that key part, it is
required to locate the first tuple with next first key part value: {2, 0}.
After that, auxiliary iterator is created and positioned to that tuple (see
schema below). Since order for the second key part is different, auxiliary
iterator moves towards main iterator.

```
[1, 0], [1, 0], [1, 1], [2, 0] ... // tuples are arranged as in B-tree
^                      ^
|               <----- |
Main iterator         Aux. iterator
```
Note that auxiliary iterator is assumed to process all keys between its initial
position and main iterator position (since those keys are ordered using other
direction - sort of full scan). Auxiliary iterator is required for each key
part starting from that which direction is different from one of first key part.
So that iteration process is likely to be slow without any optimizations.
For EQ iterator type it is possible to simply skip those tuples which doesn't
satisfy equality condition. In turn, it results in necessity to extract part of
key value for all 'EQ' iterator key parts and compare it with reference key
value. This algorithm can be generalized for any number of key parts in index.  

Pros (+):
 - it allows to specify any sets of key part iteration orders;
 - in contrast to the second implementation, the first resulting tuple is
   returned way much faster (since there's no time overhead to built new tree);
 - finally, there's almost no memory overhead.  

Cons (-):
 - obviously the main drawback of this approach is time complexity -
   it doesn't seem to be way faster than simple full-scan (the more key parts
   with different iterating order are declared, the slower process will be).

### Solution №2

Since BPS tree is built without acknowledge in which order keys should be
placed, it is assumed that order is always ascending: keys in blocks are sorted
from smaller to bigger (left-to-right); comparison between keys is made by
tuple_compare_with_key() function. It makes given tree be unsuitable for
efficient iteration in different orders. On the other hand, it is possible to
build new *temporary in-memory* BPS-tree featuring correct key order. It seems
to be easy to achieve since keys order depends on result of comparator function.
Reverting result of comparison for key parts corresponding to opposite iteration
direction gives appropriate keys ordering in the tree. Note that not all data in
space is needed to be in tree (in case of non-empty search key); only sub-tree
making up lower or upper bound of first key part is required.

Pros (+):
 - any sets of key part iteration orders are allowed.  

Cons (-):
 - first tuple to selected is probably returned with significant delay;
 - tree memory construction overhead (only during iteration routine).

### Solution №3

It extends solution №2 in sense it allows specifying sorting direction for
each part right in key def, that is during index creation. For instance:

`s:create_index('i', {parts = {{1, 'integer', 'asc'}, {2, 'integer', 'desc'}}})`

After that 'GT'/'LT' iterators for parts with declared 'desc' sorting order will
return reversed results of comparison, so only comparators are affected.
That's it (probably the simplest solution; what is more 'DESC' index is casual
SQL feature in other DBs).  

Pros (+):
 - index search via 'desc' iterators is almost as fast as via casual
   iterators;
 - this approach seems to be easy in implementation and resolves
   problem in SQL (since at the moment of ephemeral space creation it is allowed
   to set its PK key parts orders).  

Cons (-):
 - 'desc' indexes are not versatile - user is unable to set different
    orders in iterator;
 - order of iteration itself is immutable. As a result, for each different
   iteration order user has to create separate index which in turn consumes
   additional memory and time as any other index.
