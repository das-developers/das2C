/* das3_vec - Collect sets of scalers into a vectors in defined frames */


/* Not implemented yet, but needed for das2 and ACE. Collecting scalars to
   make vectors ...



"   -V PATH/DIMS:FRAME[/COMPS], --vector=PATH/DIMS:FRAME[/COMPS]\n"
"               Collect a set of dimensions into a vector definition.  Das2 data\n"
"               sources did not have the notion of a vector, so this is useful\n"
"               for rotating vectors for older sources.  DIMS and COMPS are\n"
"               comma separated lists, see examples below.\n"
"\n"
"   4. Read Juno/FGM data from a das2 data source in cartesian payload\n"
"      coordinates and convert to IAU_JUPITER in spherical coordinates (RTN):\n"
"\n"
"      " PROG " jno_metakern.tm -V *_/data/Bx,By,Bz:JUNO_FGM -R IAU_JUPITER,sph\n"
"\n"
    5. Read TRACERS/ACE data and convert the boresight look directions into GSM:\n"
"\n"
"      " PROG "tra_metakern.tm -V *_/coord/angle:TSCS/theta -R GSM\n"
*/