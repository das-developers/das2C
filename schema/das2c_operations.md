# What das2C understands about `<ops>` elements

A das3 stream can say what a group of numbers *is* without requiring that the
reader know.  Three numbers carrying an `<ops kind="geovec">` annotation are a
vector to a client that understands vectors, and three labeled lines to a client
that doesn't.  Both readings are correct and both are useful, so the format keeps
them apart on purpose.

This file lists the formalisms das2C understands and the parameters each one
takes.  The schema can't check any of it.  `<ops>` permits any attribute so that
an unrecognized formalism survives a round trip, and the price of that is that
the rules live here instead of in the `.xsd`.

Note the scope.  `kind=` is an open token, so this is not a list of every
formalism a das3 stream may legally carry.  It is a list of the ones *this
library* acts on.  Another reader may understand kinds das2C does not, and das2C
will still carry those through a round trip untouched.

## Three readers, not one

The arrangement is easier to see from the reading side.  A das3 stream has three
plausible consumers:

  1. A **codec** reads `<packet>` and nothing else.  It moves bytes into arrays
     and never learns what a single value means.

  2. A **structural client** reads the element name, `intern=`, `units=` and the
     labels.  It knows there are three numbers per location and what to call them,
     which is enough to plot.  It skips `<ops>` as one element.

  3. A **formalism client** opens `<ops>`, switches on `kind=`, and can then take
     a magnitude, rotate into another frame, or add two vectors.

Reader 2 is the one that pays for this design.  If the math were part of the
element name, a `<geovec>` element would lock out every client that had not
implemented vectors, because the thing it doesn't understand would be the name of
the element it has to read.  With the math in a child element instead, an
unhelpful `kind=` costs the reader nothing but a skipped subtree.

## The element carries no children

Two rules, both narrow, both essential:

```
   <ops kind="geovec" frame="TSCS" system="cartesian" sysorder="0;1;2"/>
```

  1. `kind=` is required and names the math.  Every *other* attribute is a
     parameter of that kind.  Though this library always outputs it first,
     XML attributes are unordered so may appear anywhere in the element.

  2. An `<ops>` element never has children.  This is what makes an unknown kind
     preservable: attributes are name/value pairs, and a reader can carry a bag
     of those through to its output without understanding any of them.  Nested
     structure could not survive the same trip without a full DOM.

A formalism that genuinely needs structure puts the structure in `<context>` and
names it from an attribute here.  That is how a frame works already.

A reader that knows a kind must refuse a parameter that kind does not define.
Silently ignoring a misspelled `sysorder` would mean shipping the wrong component
order, so a reader that recognizes `geovec` and then sees `sysordr=` has to say
so.  A reader that does *not* recognize the kind has the opposite duty: carry
every parameter through untouched, because it cannot know which ones matter.

## Shape says the layout, ops says the math

`intern=` is pure shape and never appears as an `<ops>` parameter.  A 3x3 rotation
matrix and a plain 3x3 matrix have identical layouts and differ only in their
`kind=`.  This is not hypothetical; TRACERS ships non-rotation matrices.

```
   <composite intern="3;3"> <ops kind="rotation" .../>   a rotation matrix
   <composite intern="4">   <ops kind="rotation" .../>   a quaternion
   <composite intern="3">   <ops kind="geovec" .../>     a 3-vector
```

The component count is therefore never stated twice.  Ask `intern=`.

## The kinds of operations understood by das2C

### linear

No parameters.  Ordinary numbers under ordinary arithmetic.  This is the
unstated default: a variable with no `<ops>` at all is linear.  Stating it
outright is legal and occasionally useful, but not required.

### point

No parameters.  The affine rule.  Differences are meaningful and sums are not:

```
   point - point    = interval
   point + interval = point
   point + point    = refused
```

Calendar time is the canonical case, which is why a time variable should carry
`<ops kind="point"/>`.  Adding two calendar dates means nothing, and a reader
that knows this can refuse rather than produce a number.  Any zero-referenced
axis fits the same rule -- a position measured from a chosen origin.

### complex

One complex number per point.  Pairs with `intern="2"`, and with nothing else:
exactly two components in one level.

```
   system=    rectangular | polar.  Absent means rectangular.
```

The two representations and their components, in order:

```
   rectangular   real, imaginary
   polar         magnitude, phase
```

Component 0 is the real part or the magnitude, always.  There is no `sysorder=`
here -- a producer that stores the imaginary part first has to restate its
storage, because both readings of a pair cannot coexist in one attribute.

Angles are always degrees, the same law `geovec` follows, so a polar variable's
`units=` describes the magnitude and the phase carries no units of its own.  A
rectangular variable's two components share the one `units=` between them; a
real part and an imaginary part measured on different scales would not be a
number.

Arithmetic runs in rectangular and comes back in the LEFT operand's
representation, so a polar variable stays polar through a multiply.  A plain
number entering complex arithmetic is promoted to a zero-imaginary complex,
which is why scaling needs no rule of its own.

Three complex numbers making up a spectral field is a real and common thing,
and it is a formalism yet to be written rather than this one at `intern="3;2"`.
Only one `<ops>` fits on a composite, so the pairing cannot be expressed by
stacking this kind on top of a vector.

### geovec

A geometric vector in a reference frame.

```
   frame=     a <frame> context entry, by name
   system=    the component system, see below.  Absent means cartesian.
   sysorder=  ';'-separated slots drawn from [0-2], at most three, mapping
              storage order onto the system's canonical triplet.  Absent means
              ascending.
   surface=   a <surface> context entry, by name.  Only meaningful for the
              ellipsoidal systems.
   body=      the body the frame is fixed to, a NAIF-style id or name.
```

The component systems and their canonical right-handed triplets:

```
   cartesian     x, y, z
   cylindrical   rho, phi, z          (polar is the 2-D case of this)
   spherical     r, theta, phi        ISO, colatitude, 0 at the north pole
   centric       r, phi, theta        90 at the north pole
   detic         phi, theta, altitude ellipsoidal
   graphic       phi, theta, altitude ellipsoidal, longitude reversed
```

Angles are always degrees, so the variable's `units=` describes the radial or
altitude component.  `detic` measures longitude eastward, which is the Earth
convention; `graphic` measures it westward.  A latitude/longitude/altitude
position is one `geovec` in a `detic` or `graphic` system, not three scalars --
keeping it together is what lets SPICE and friends transform it.

`sysorder` exists because storage order and canonical order are different
questions.  A stream that ships components as (z, x, y) in a cartesian frame
declares `sysorder="2;0;1"`: slot 0 holds canonical direction 2, and so on.

### rotation

A rotation between two frames.

```
   from=      the frame rotated FROM, by name
   to=        the frame rotated TO, by name
   flavor=    a distinction the shape cannot make, such as a quaternion's
              component order.
```

Matrix versus quaternion comes from `intern=` and never from `flavor=`.  What
shape genuinely cannot say is whether a four-component quaternion runs
`w,x,y,z` or `x,y,z,w`, and that is the gap `flavor=` fills.

## Reference frames and other context entries

Das stream headers provide a registry of context entries.  These supply the
supporting metadata a calculation needs but that an individual variable should
not have to carry.  Like operation kinds, not all clients understand all context
entries.  das2C provides a reference frame entry with named members for the
central body id, plus a properties block for anything else.

Providing a frame entry alongside geovec data is highly recommended but not
required.  A `frame=` or `surface=` that names an entry which does not exist is
legal: the reader interns one and hands back its id, so the stream still parses
and the vector still knows which frame it is in.  What you lose is everything the
entry would have carried.  An auto-created one holds nothing but a name -- no
title, no body, nothing a client could use to label an axis or drive a transform.

`body=` is a property of the *frame*, not of the vector, so a reader folds it into
the frame's context entry rather than keeping a copy on each variable.  Two
variables in the same frame cannot then disagree about the body.

A `geovec` with no `frame=` at all reads as frameless.  That is also legal, also
discouraged, and shows up as a `noframe` reference: somebody declared a vector
and did not bother to say where it points.  A component system cannot be resolved
without a frame, so such a vector can be plotted but not transformed.

## An unknown kind is not an error

A reader that meets `kind="wildcat"` keeps the token and the attributes, hands out
the numbers, and refuses arithmetic.  That last part is the point.  Rules the
library does not know are rules it must not guess, so "unknown formalism" is a
defined outcome rather than a silent wrong answer.

What such a variable still offers is not a consolation prize: N numbers per item
in the right internal shape, plus the property dictionary.  If the application
knows what they mean, that is everything it needs.

## Status in das2C

Recognized today, with the affine and ordinary-arithmetic rules registered:

```
   linear      yes
   point       yes
   complex     yes, all four operators, both representations
   geovec      recognized, packs a composite datum; addition, subtraction and
               scaling; no dot or cross product yet
   rotation    recognized; applying one to a vector is not built yet
```

What is still ahead is the NAMED products: dot, cross, and applying a rotation.
Reading, writing and round-tripping every kind above works regardless,
including the ones not defined here.

`das3_cdf` does not yet write a complex variable back out to CDF.  It halts
with a not-implemented error rather than dropping the component labels, since
a bare length-2 axis is indistinguishable from a two channel bundle and the
round trip would quietly stop being one.
