# Developer notes for DasVar_subset() and _DasVar_strideSubset()

DasAry is unique in that it allows all array dimensions to be completly ragged.
This is rare.  Most major binary array implementations such as those in NumPy, 
IDL, MatLab, require that all internal arrays satisfy the strided assumption.
This is reasonable as the majority of real world datasets can be accessed 
via strided indexing.  And those that can't are often fill-padded to so that
strided indexing applies.

Since strided arrays are common, the `DasVar_subset()` function switches over
to strided copies when the extraction region satisfies the strided condition.
This allows for copy out in a loop that skips the slow top-down access function
`DasAry_getAt()`.  In addition, if the requested subsection is actually a 
continous range of memory with no repeats, then a simple memcpy() is used to
speed up the process further.

To understand the internal implementation of DasVar_subset() a bit more, some
background is useful.

## Strided Arrays

Strided arrays are ones for which the offset inside the 1-D memory allocation
can be described by the equation:
```
            rank-1
            ---
   offset = \    Step  *  I
            /        d     d
            ---
            d=0
```
Where:
```
            rank-1
            ---
 Step    =  | | Size 
     d      | |     d
            S=d+1
```                
For example given an array with the shape (10, 6, 4), the offset is:
```
   offset = 24*i + 4*j + K
```	
Iterating over all i, j, and k values in a loop will produce all the offsets
needed to read out the data value.  For the array above the iteration indices
are added to read all values:
```
                |<10     |<6      |<4
   offset = 24*i|   + 4*j|   + 1*k|
                |0       |0       |0
```

## Slicing alters iteration bounds

To slice an array we hold one (or more) of the indices constant and then 
iterate over the rest.  The equation is not changed, only the iteration
indicies for one or more items.  For example to get a slice at j = 2 
then:

```
                |<10     |<3      |<4
   offset = 24*i|   + 4*j|   + 1*k|
                |0       |2       |0
```

Is all we need to do.

## Degeneracy alters the step size

For data arrays than are degenerate in one or more indexes of the overall
dataset, the offset calculation is independent of the value of a given index.
In those cases the step size is 0.  

Take for example an array of frequencies that are really only a function of j.
Reading out all the values for the overall dataset from the array would be
handled by:
```
               |<10     |<6      |<4
   offset = 0*i|   + 1*j|   + 0*k|
               |0       |0       |0
```

## Remapping changes the lookup index

The real frequency array mentioned above only has a single index.  Thus any
requests to increment dataset index **j** actually increment the hidden index
**I**.  Other indicies are retained as loop counters, but don't actually 
change an offset address.  Thus the offset function for our frequency array
of size 6 is actually:
```
               |<10     |<6      |<4
   offset = 0*i|   + 1*I|   + 0*k|
               |0       |0       |0
```
where i and k are just loop counters.  In fact all DasVar objects that are
backed by arrays remap the loop counter indexes to real indexes via the `idxmap`
member.

When indexes are remapped the stride coefficient and any non-degenerate ranges
must move with the remapping.  For example take the intrinsic array (14,17):
```
                |<14     |<17
   offset = 17*I|   + 1*J|  
                |0       |0 
```
This could be remapped to reverse the indices and to add a degenerate dimension
of size 20 for each given entry, this would have the offset relation:
```
               |<17      |<14     |<20
   offset = 1*J|   + 17*I|   + 0*k|
               |0        |0       |0
```
where the loop counter i now corresponds to the real memory index J, the loop
counter j now corresponds to the real memory index I, and the loop counter k
just counts.

## Continuous Ranges

Multidimensional loops of the type needed to deal with an arbitrary number of
array dimensions can have lots of conditionals than need to be checked in each
loop iteration.  This makes such loops much slower than a simple `memcpy()` of
a continuous range.  We need to know when a continous range is requested so that
the code can switch over to a fast memcpy call when possible.

A continuous range slice:

  1. Does not *iterate* over any degenerate dimensions, i.e. all degenerate
     dimensions have been sliced down to a single value.
	  
     This condition means that we don't need to repeat values during copy out.

  2. Has a continous iteration range in slowest moving requested index
     and covers the complete range of remaining backing array dimensions
     
  3. The backing array indexes have not be remapped out of order.

For example, slicing a direct mapped array with shape (10, 5, 4) dataset on the
range i = 2..5, provides a continuous range:
```
                 |<5     |<6      |<4
   offset =  24*I|  + 4*J|   + 1*K|
                 |2      |0       |0
```
The top of the range is given by the address:
```
   base_offest = base_ary_ptr + 24*2 + 4*0 + 1*0 = base_ary_ptr + 48
	output_size = 3*6*4 = 72
```
Slicing our frequency array which has shape (-, 5, -) for i = 7 would require
a loop as each value has to be copied 4 times in a row to the output due to
the degeneracy in k:
```
                |<8     |<6      |<4
   offset =  0*i|  + 1*I|   + 0*k|
                |7      |0       |0
```
However, if the user were to ask for a slice at (7,*,2) then we could again
output a single pointer for the whole dataset:
```
                |<8     |<6      |<3
   offset =  0*i|  + 1*I|   + 0*k|
                |7      |0       |2

   base_offset = base_ary_ptr + 0*7 + 1*0 + 0*2 = base_ary_ptr
   output_size = 1*6*1 = 6
```

### Optimization for continous ranges

Before the main copy loop runs, it inspects the requested slice from fastest
moving index to slowest moving index.  Each output dimension that satisfies
the continous range condition is marked as memcpy'able.  The copy out loop is
short circuited for such ranges.  If the slowest moving index over which iteration
is to occur is memcpy'able, then the entire copy loop is short circuited.

-*cpiker*
