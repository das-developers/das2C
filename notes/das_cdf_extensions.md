# das3 extensions to ISTP CDF metadata

Status: draft, 2026-10-16.  To be folded into the das2C ICD.

das3 streams carry structure that ISTP CDF metadata has no words for: the
component system of a vector, the origin of a position, a time offset that
varies along a non-time index.  `das3_cdf` writes that structure into CDF
variable attributes and `das3_from_cdf` reads it back, so a das3 stream can
round trip through a CDF file without losing it.

These attributes are mission independent.  Nothing here is specific to any
one mission, and nothing in the das2C project reads a mission-specific
attribute name directly.  A mission that already spells one of these concepts
differently (for example `COORD_FRAME` for the frame) maps its name onto
ours with the `-p FROM:TO` option of both programs.

## Naming rules

1. **ISTP first.**  If the ISTP guidelines, or the ISTP metadata proposal at
   istp-metadata.readthedocs.io, name a concept, that name is used.
2. **Extension names next.**  A concept on the list below gets an
   official-looking upper case name.  If ISTP later adopts the concept under a
   different name, the property map handles the rename, and if ISTP adopts
   ours, nothing changes.
3. **`ops*` for everything else.**  A das3 `<ops>` parameter with no entry in
   either list is written as `ops` + the parameter name in camel case:
   `opsKind`, `opsSurface`.  A lower case first letter can never
   collide with an ISTP name.  These are das-internal and other readers 
   are not expected to understand them.

## Variable attributes

### COMPONENT_SYSTEM

The the components of a vector, position, complex number or rotation are 
provided in .  This is *not* the frame (the frame is `FRAME_NAME`, see below).
The frame defines cardnal directions, the component system states which
triplets are provided.

| Value         | Applies to        | Canonical components, in order |
|---------------|-------------------|--------------------------------|
| `cartesian`   | vector, position  | x, y, z                        |
| `cylindrical` | vector, position  | rho, phi, z                    |
| `spherical`   | vector, position  | r, theta, phi (ISO 31-11, theta is colatitude) |
| `centric`     | vector, position  | r, lambda, phi (phi is latitude) |
| `detic`       | position only     | lambda, phi, h (eastward longitude) |
| `graphic`     | position only     | phi, lambda, h (westward longitude) |
| `rectangular` | complex           | real, imaginary                |
| `polar`       | complex           | magnitude, phase               |
| `matrix`      | rotation          | 3x3 direction cosines          |
| `quaternion`  | rotation          | 4 elements                     |

All angles are in degrees.  The variable's `UNITS` describe the radial or
length components; `UNIT_PTR_n` gives per-component units when they differ.

Absent means the reader has to guess from the component labels, which is
what `das3_from_cdf` does for files that predate this attribute.

### COMPONENT_ORDER

Which canonical component each stored component is, in storage order,
`;`-separated, using the ASCII names from the table above.  Written for
vectors and positions, can be omitted if the connonical right-handed
order above is used.

A variable may store fewer components than its system has.  Missing ones
take the system's default: zero for an angle, a cartesian component, or a
detic/graphic height, and one for a radius, in the variable's `UNITS`.  So an
angles-only vector in degrees reads as a unit direction, and an angles-only
position in `R_E` lies on the reference sphere.

Example: an anode look-direction table that stores only the polar angle of a
spherical direction:

    UNITS             = "degrees"
    COMPONENT_SYSTEM  = "spherical"
    COMPONENT_ORDER   = "theta"
    FRAME_NAME        = "TS2_ACI_REDUCED"

A one-component variable like this is written as a plain CDF variable, with
no component dimension and no `LABL_PTR_n`. The four attributes above mark
it as one component of a direction.

Readers accept the canonical Unicode symbols (r, &theta;, &phi;, &rho;,
&lambda;, h, x, y, z) in place of the ASCII names.

Angles are *not* normalized.  A polar angle may run from -90 to 270 degrees for
an instrument whose look directions sweep a full circle in one plane; the
values are what the instrument reports, and `AVG_TYPE` (below) tells a reader
they are periodic.

### AUX_DEPEND_n

An additional coordinate variable along CDF dimension `n`, beyond the one in
`DEPEND_n`.

ISTP allows one `DEPEND_n` per dimension.  A das3 dataset can have several
coordinates along one index; the commonest case is a particle spectrum whose
energy steps are taken at different times, so that index 1 has both an
energy and a time offset.  `das3_cdf` gives `DEPEND_n` to the coordinate that
adds a new physical dimension (energy) and names the other in `AUX_DEPEND_n`.

    ts2_l1b_aci_flux:  DEPEND_0     = "Epoch"
                       DEPEND_1     = "ts2_l1b_aci_energy"
                       AUX_DEPEND_1 = "EpochOffset"
                       DEPEND_2     = "ts2_l1b_aci_ion_velocity_dir"

How the auxiliary coordinate combines with the others is said by the target
variable's own attributes.  For a time offset that is `OFFSET_OF`.

One variable per dimension.  If a dataset has more than one leftover
coordinate along the same index, the first is written and the rest are
reported as a warning.

`n` is 1 or greater.  A record varying coordinate along the record index
(spacecraft location, say) already points at time through its own
`DEPEND_0` and needs no `AUX_DEPEND_0`.

### OFFSET_OF

In a offset variable this provides the the name of the time variable it is
an offset from.  This is most commonly used for fine time offsets from a
record start time. A reader can find the offset values either as a 
`DEPEND_n` (waveform data, where there is no other physical dimension changes
along that index) or as an `AUX_DEPEND_n`.

### AVG_TYPE

From the ISTP metadata working group's variable attribute list
(istp-metadata.readthedocs.io), optional for data and record varying
support_data.  `das3_cdf` passes it through from the stream property of the
same name.  Listed values: `standard`, `angle_degrees`, `angle_radians`,
`angle_hour`, `RMS`, `log`, `decibel`, `cosine`, `none`, `Geometric`.
`angle_hour` covers 24-hour periodic quantities such as magnetic local time.
das may extend the value set where a quantity needs it; CDAWeb tools tolerate
unknown values.

das3 has no formalism for a periodic value yet, so `AVG_TYPE` is the only
place this information lives.  Reducers that average must honor it.

### FRAME_NAME, FRAME_FIXED

The reference frame of a vector or position, as a SPICE style frame name
(`TSCS`, `IAU_EARTH`, `GSE`), and, for a position, whether it rotates with
the origin body.

| Attribute      | Meaning                                                    |
|----------------|------------------------------------------------------------|
| `FRAME_NAME`   | the frame's name, which way the axes point                 |
| `FRAME_FIXED`  | positions only: `true` when the frame is fixed to the `FRAME_ORIGIN` body and rotates with it, in the SPICE sense.  Written only when true; absent means not fixed or not stated.  A free vector has none: whether its frame is body-fixed is part of the frame's definition |

The ISTP metadata proposal spells the frame name `COORDINATE_SYSTEM`.  das
does not use that name: it reads too much like `COMPONENT_SYSTEM`, which is a
different thing (cartesian versus spherical, not GSE versus TSCS).  A
consumer that wants the proposal spelling gets it with
`das3_cdf -p FRAME_NAME:COORDINATE_SYSTEM`, and `das3_from_cdf` reads
`COORDINATE_SYSTEM` as the frame when `FRAME_NAME` is absent.

The body a frame is attached to (SPICE's frame center) is not written.  It
is part of the frame's definition, and on a vector it would read as an
origin.  A position's origin and frame are independent: Cassini relative to
Titan in IAU_SATURN has `FRAME_ORIGIN=TITAN`, `FRAME_NAME=IAU_SATURN` and no
`FRAME_FIXED`, since IAU_SATURN does not rotate with Titan.

### ISTP proposal attributes written alongside

From istp-metadata.readthedocs.io, written when a das3 form has the concept:

| Attribute           | Source                                         |
|---------------------|------------------------------------------------|
| `FRAME_ORIGIN`      | the origin body of a position (`opsKind=position`) |
| `TENSOR_ORDER`      | 1 for vectors and positions, 2 for a rotation matrix; omitted for one-component variables, which have no component dimension |
| `REPRESENTATION_n`  | a metadata variable holding the component symbols, for multi-component variables only |

### ops* attributes (das internal)

| Attribute     | Meaning                                                   |
|---------------|-----------------------------------------------------------|
| `opsKind`     | the das3 formalism: `vector`, `position`, `complex`, `rotation`, `point`, or a generic kind name |
| `opsSurface`  | the reference ellipsoid of a `detic` or `graphic` position |
| `opsFrom`, `opsTo` | the frames a rotation maps between                   |
| `opsSysorder` | storage order of a rotation's elements, das3 slot numbers |
| `ops<Param>`  | any parameter of a generic (extension) `<ops>` kind       |

A position (`opsKind=position`) is told apart from a free vector by
`opsKind`, or failing that by the presence of `FRAME_ORIGIN`.

### plotGroup (das internal)

The das3 `group=` of the dataset a data variable came from.  A group means
"plot these together": the bands of a multi-receiver survey, or a cyclotron
frequency line over a spectrogram.  CDF has no datasets, so the group is
written on each data variable instead.

* Written by `das3_cdf` on every data variable of a dataset that has a
  group, and on nothing else.  Datasets without a group (the null group)
  write no `plotGroup`, so files from such streams don't change.
* Never written on coordinate variables: one coordinate can serve datasets in
  different groups.
* Read by `das3_from_cdf` as part of the dataset key, together with the
  DEPEND chain, so variables on one time base but in different groups come
  back as separate datasets, each in its group.  `das2_from_cdf` ignores
  it: the das2.2 schema has no packet attribute for a group.
* Groups are never inferred.  A file without `plotGroup` reads back with
  every dataset in the null group, even when variables share a time base or
  a `DICT_KEY` class.

## Global attributes

Proposed, not yet written: one global attribute naming this convention set
and its version, with a URL to this document, in the manner of the NetCDF CF
`Conventions` attribute.  That is the one place "das3" would appear in a
file; the variable attributes above stay generic.

## Reading files that predate these attributes

`das3_from_cdf` falls back, in order, to:

* the component system spelled by the `LABL_PTR_n` labels (`B_x B_y B_z`,
  `r_GEO theta_GEO phi_GEO`),
* a frame from `COORDINATE_SYSTEM`, or from a mission attribute mapped onto
  `frame` with `-p ATTR:frame` (TRACERS files before these extensions use
  `COORD_FRAME`),
* no auxiliary coordinates.  A time offset table that no variable points to
  is listed as ignored.
