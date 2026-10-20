#!/usr/bin/env bash

# Dataset groups through CDF files (issue #25).  CDF has no datasets, so
# das3_cdf writes a dataset's group= as a plotGroup attribute on its data
# variables, and das3_from_cdf rebuilds groups from it.  Groups are never
# inferred on the way back in.
#
#   1. Juno survey, four bands in one das2 group: das2 -> CDF -> das3 keeps
#      the group, and grouped names are always qualified (spec_dens_10, not
#      a bare spec_dens for whichever band came first).
#   2. TRACERS ACI, no groups: no plotGroup is written, and the round trip
#      invents none.
#   3. das2_from_cdf doesn't write <packet group=>, which das2.2 lacks.

BD=$1

f=ex09_juno_waves_survey
echo "Testing: plotGroup round trip, $f.d2t -> CDF -> das3"
rm -f $BD/${f}_grp.cdf
$BD/das3_cdf -l warning -i examples/$f.d2t -o $BD/${f}_grp.cdf -r
if [ "$?" != "0" ]; then echo " Result: FAILED (das3_cdf errored on $f.d2t)"; exit 4; fi
sOut=$($BD/das3_from_cdf -l warning $BD/${f}_grp.cdf | $BD/das3_text | grep -o '<dataset name="[^"]*" group="[^"]*"' | tr '\n' ' ')
sExpect='<dataset name="spec_dens_10" group="spec_dens" <dataset name="spec_dens_20" group="spec_dens" <dataset name="spec_dens_30" group="spec_dens" <dataset name="spec_dens_40" group="spec_dens" '
if [ "$sOut" != "$sExpect" ]; then echo " Result: FAILED (got: $sOut)"; exit 4; fi
echo " Result: PASSED"
echo

f=ex09_juno_waves_survey
echo "Testing: variable map names are exact, $f.d2t + ex09_waves_survey_varmap.conf"
rm -f $BD/${f}_map.cdf
$BD/das3_cdf -l warning -i examples/$f.d2t -o $BD/${f}_map.cdf -r -m examples/ex09_waves_survey_varmap.conf
if [ "$?" != "0" ]; then echo " Result: FAILED (das3_cdf errored with the map)"; exit 4; fi
sOut=$($BD/das3_from_cdf -l warning $BD/${f}_map.cdf | $BD/das3_text | grep -o '<dataset name="[^"]*" group="[^"]*"' | tr '\n' ' ')
sExpect='<dataset name="LFR_LO" group="spec_dens" <dataset name="LFR_HI" group="spec_dens" <dataset name="HFR_LO" group="spec_dens" <dataset name="HFR_HI" group="spec_dens" '
if [ "$sOut" != "$sExpect" ]; then echo " Result: FAILED (got: $sOut)"; exit 4; fi
# Every map entry must have matched: all twelve names, no fallback Epoch_*
for v in LFR_LO LFR_LO_Epoch LFR_LO_Freq LFR_HI LFR_HI_Epoch LFR_HI_Freq \
         HFR_LO HFR_LO_Epoch HFR_LO_Freq HFR_HI HFR_HI_Epoch HFR_HI_Freq; do
	n=$($BD/das3_from_cdf -n $BD/${f}_map.cdf 2>/dev/null | grep -cE "CENTER +$v +")
	if [ "$n" = "0" ]; then echo " Result: FAILED (no CDF variable $v)"; exit 4; fi
done
echo " Result: PASSED"
echo

f=ex20_tracers_aci
echo "Testing: no invented groups, $f.d3t -> CDF -> das3"
rm -f $BD/${f}_grp.cdf
$BD/das3_cdf -l warning -i examples/$f.d3t -o $BD/${f}_grp.cdf -r
if [ "$?" != "0" ]; then echo " Result: FAILED (das3_cdf errored on $f.d3t)"; exit 4; fi
n=$($BD/das3_from_cdf -l warning $BD/${f}_grp.cdf | $BD/das3_text | grep -c '<dataset [^>]*group=')
if [ "$n" != "0" ]; then echo " Result: FAILED ($n datasets came back with a group)"; exit 4; fi
echo " Result: PASSED"
echo

f=ex09_juno_waves_survey
echo "Testing: das2_from_cdf stays inside the das2.2 schema, ${f}_grp.cdf"
sOut=$($BD/das2_from_cdf -l warning $BD/${f}_grp.cdf 2017-01-01 2017-01-02 spec_dens_20 | grep -a -o '<packet[^>]*>')
if [ "$sOut" != '<packet>' ]; then echo " Result: FAILED (got: $sOut)"; exit 4; fi
echo " Result: PASSED"
echo

exit 0
