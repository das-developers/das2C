# What das2C understands about `<ops>` elements

A das3 stream can say what a group of numbers *is* without requiring that the
reader know.  Three numbers carrying an `<ops kind="vector">` annotation are a
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
element name, a `<vector>` element would lock out every client that had not
implemented vectors, because the thing it doesn't understand would be the name of
the element it has to read.  With the math in a child element instead, an
unhelpful `kind=` costs the reader nothing but a skipped subtree.

## The element carries no children

Two rules, both narrow, both essential:

```
   <ops kind="vector" frame="TSCS" system="cartesian" sysorder="0;1;2"/>
```

  1. `kind=` is required and names the math.  Every *other* attribute is a
     parameter of that kind.  Though this library always outputs it first,
     XML attributes are unordered so may appear anywhere in the element.

  2. An `<ops>` element never has children.  This is what makes an unknown kind
     preservable: attributes are name/value pairs, and a reader can carry a bag
     of those through to its output without understanding any of them.  Nested
     structure could not survive the same trip without a full DOM.

A formalism that genuinely needs structure has nowhere to put it, and that is the
trade.  External things -- a frame, a reference ellipsoid, a body -- are named by
plain attribute values here and resolved elsewhere.

A reader that knows a kind must refuse a parameter that kind does not define.
Silently ignoring a misspelled `sysorder` would mean shipping the wrong component
order, so a reader that recognizes `vector` and then sees `sysordr=` has to say
so.  A reader that does *not* recognize the kind has the opposite duty: carry
every parameter through untouched, because it cannot know which ones matter.

## One units value per composite

A composite is one quantity with several components, so it carries one
`units=` and every component shares it.  A list of units is not legal.  Any
angular component, the phase of a polar pair or the angles of a spherical or
geodetic system, is in degrees so angular units are implicit.

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

Angles are always degrees, the law `vector` and `geoloc` follow, so a polar variable's
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

### vector

A geometric vector in a reference frame, with no origin: a magnetic field, a
velocity, a displacement.  Something with direction and magnitude that is not
measured *from* anywhere.

```
   frame=     the frame's name.  Optional: a frameless vector is legal, it just
              cannot be checked against anything.
   body=      the body the FRAME is fixed to.  It describes the frame and rides
              here because there is no frame object to fold it into.
   fixed=     true when the frame does not rotate.
   system=    cartesian | cylindrical | spherical | centric.  Absent means
              cartesian, which is what nearly all measured data is.
   sysorder=  ';'-separated slots drawn from [0-2], at most three, mapping
              storage order onto the system's canonical triplet.  Absent means
              ascending.
```

`surface=` and `center=` are refused here.  A reference ellipsoid and an origin
belong to a position, and a stream supplying either is describing one.

Free vectors add, subtract and scale.  A stream may send fewer than three
components and mean it: a two-component field is measured data, not a
truncation, and an absent component takes its system's default, zero for a
direction and one for a radius.

### geoloc

A position: the same components as a vector plus an ORIGIN.  Spacecraft
ephemeris, a ground station, a sub-satellite track.  The origin is what changes
the algebra.  A position is the three dimensional case of the affine rule
`point` follows for calendar time:

```
   geoloc - geoloc  = vector     the displacement from one to the other
   geoloc + vector  = geoloc
   vector + geoloc  = geoloc
   geoloc - vector  = geoloc
   geoloc + geoloc  = refused    adding two positions means nothing
   geoloc * number  = refused    a position cannot be scaled in place
```

```
   body=      the ORIGIN body's name.  Required: without one the values are a
              free vector and belong under `vector`.
   frame=     the frame's name.  Required in practice, since a position has to
              be expressed in some frame.
   surface=   the reference ellipsoid's name.  Only meaningful for the
              ellipsoidal systems.
   fixed=     true when the frame does not rotate.
   system=    any of the six below.  Absent means cartesian.
   sysorder=  as for `vector`.
```

A frame's body and a position's body are different things.  Cassini relative
to Saturn expressed in IAU_JUPITER: the frame is fixed to Jupiter, the origin is
Saturn.  A position only ever needs its origin, so `body=` is spent on that.

The component systems and their canonical right-handed triplets.  The first
four serve both kinds; the two ellipsoidal ones are measured on a body and so
belong to `geoloc` alone:

```
   cartesian     x, y, z
   cylindrical   rho, phi, z          (polar is the 2-D case of this)
   spherical     r, theta, phi        ISO, colatitude, 0 at the north pole
   centric       r, lambda, phi       no reference surface, 90 at the pole
   detic         lambda, phi, h       ellipsoidal, longitude EAST
   graphic       phi, lambda, h       ellipsoidal, longitude WEST
```

`lambda` is longitude, `phi` is latitude and `h` is height above the reference
ellipsoid.  Angles are always degrees, so the variable's `units=` describes the
radial or height component.  A latitude/longitude/altitude position is one
`geoloc` in a `detic` or `graphic` system, not three scalars -- keeping it
together is what lets SPICE and friends transform it.

Every canonical triplet above is RIGHT HANDED, and that rule is what sets
`graphic`'s order apart from `detic`'s.  Held in a common longitude-first order
a westward longitude would give a left-handed triad, so latitude leads instead.
A reader must not assume the two ellipsoidal systems share a slot order.

`detic` measures longitude eastward and `graphic` westward; that is the entire
difference between them, since the two share a reference ellipsoid and a
latitude definition.  `graphic` is west-positive by definition here.  Where a
body's planetographic longitude runs east -- retrograde rotators, and by
convention the Earth, Moon and Sun -- the system is `detic` and must be spelled
that way; there is no east-handed spelling of `graphic`.

`sysorder` exists because storage order and canonical order are different
questions.  A stream that ships components as (z, x, y) in a cartesian frame
declares `sysorder="2;0;1"`: slot 0 holds canonical direction 2, and so on.

### rotation

A rigid rotation between two frames: lengths and handedness are preserved, so
there is no scaling, shear or reflection.  A transform that stretches or
reflects is a different kind with its own rules, not this one carrying a flag.

```
   from=      the frame rotated FROM, by name
   to=        the frame rotated TO, by name
   system=    matrix | quaternion.  Absent means matrix.
   sysorder=  ';'-separated slots mapping storage order onto the system's
              canonical element order.  Absent means ascending.
```

`system=` names the representation and `intern=` must agree with it: a matrix
is `intern="3;3"` and a quaternion is `intern="4"`.  No other shape is a
rotation.  The representation is never inferred from the shape; a four element
run without `system="quaternion"` is refused, not guessed at.

The canonical element order for each representation, which is what `sysorder=`
indexes:

```
   matrix        0 xx  1 xy  2 xz  3 yx  4 yy  5 yz  6 zx  7 zy  8 zz
   quaternion    0 w   1 x   2 y   3 z
```

A matrix is row major, and the row is the `to=` axis while the column is the
`from=` axis: applying `R` to a vector `v` computes `out[r]` as the sum over `c`
of `R[3r+c] * v[c]`.  Reading the two letters backwards applies the inverse.
Canonical quaternion order is scalar first, SPICE's convention.

`sysorder=` for a rotation is a permutation of the whole element list, not a
subset.  Storage slot `i` holds canonical element `sysorder[i]`.  A producer
holding a column major matrix writes `sysorder="0;3;6;1;4;7;2;5;8"`; one holding
a scalar-last quaternion, which most attitude packages do, writes
`sysorder="1;2;3;0"`.  Neither reorders on the way out.

Every element must be placed.  A vector may omit components because each
absent one has a default; no element of a rotation has one, so a list shorter
than the representation's count is refused.  That rules out the three element
quaternion whose scalar is implied by unit length.  A later stream version may
take that up.

## Reference frames, surfaces and bodies

There is no registry.  A stream header carries no table of frames or surfaces to
look these names up in, and none is wanted: whoever ACTS on a name is the one
positioned to say whether it is real.  das3_spice handing a frame name to
`namfrm_c` is the case in mind.  It can answer; a reader cannot, and a reader
that pretended to would be guessing.

So `frame=`, `surface=` and `body=` are plain strings that ride on the variable
itself.  Two variables can therefore disagree about the body behind one frame
name.  The library carries both and the consumer decides.

A vector with no `frame=` at all reads as frameless.  That is legal and
discouraged: somebody declared a vector and did not say where it points.  A
component system cannot be resolved against anything without a frame, so such a
vector can be plotted but not transformed.

`surface=` names the reference ellipsoid the ellipsoidal systems measure
against.  A producer that has a datum name should give it -- `surface="WGS84"`
-- but many have none to give.  SPICE, for one, keys a body's triaxial ellipsoid
off `BODYnnn_RADII` and never names it; its only named surfaces are DSK shape
models, which are a different thing.  A producer working from a text PCK has
nothing to point at but the body, so an absent `surface=` on an ellipsoidal
system reads as "the reference ellipsoid of `body=`, per whatever the consumer
loads."

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
   vector      recognized, packs a composite datum; addition, subtraction and
               scaling; no dot or cross product yet
   geoloc      recognized, packs a composite datum; the affine rules above,
               a vector being the difference of two positions
   rotation    both representations read, written and labelled in storage
               order; a matrix applies to a cartesian vector and composes
               with another matrix.  Quaternion arithmetic is not built.
```

What is still ahead is the NAMED products, dot and cross, and quaternion
arithmetic.  Reading, writing and round-tripping every kind above works
regardless, including the ones not defined here.

`das3_cdf` writes every kind above to CDF: one trailing dimension per
composite with a LABL_PTR naming the components, UNIT_PTR where the law puts
degrees on some of them, the `<ops>` element as `opsKind` and `ops<Param>`
attributes, and the ISTP proposal attributes (COORDINATE_SYSTEM, TENSOR_ORDER,
REPRESENTATION_1, FRAME_ORIGIN) where a kind has an equivalent.  A multi-level
internal shape is flattened in C order; see the das3_cdf help text.
