#!/usr/bin/env bash

echo "Testing: Das Fix-time tool"

BUILD_DIR=$1

echo "Running: whole value addition/subtraction tests..."
${BUILD_DIR}/das1_fxtime 2004-000T12:30:59.375 > ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2004-001T12:30:59.375 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2004-01-01T12:30:59.375 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2003-366T12:30:59.375 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2005--366T12:30:59.375 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2005--12-01T12:30:59.375 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2004-100T12:30:59.375 -j 99 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2003-01-01T12:30:59.375 +y 1 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2003-01-01T12:30:59.375 +j 365 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2003-01-01T12:30:59.375 +h 8760 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2003-01-01T12:30:59.375 +m 525600 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2003-01-01T12:30:59.375 +s 31536000 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2005-01-01T12:30:59.375 -y 1 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2005-01-01T12:30:59.375 -j 365 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2005-01-01T12:30:59.375 -h 8760 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2005-01-01T12:30:59.375 -m 525600 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2005-01-01T12:30:59.375 -s 31536000 >> ${BUILD_DIR}/das1_fxtime_test.txt

# half stuff
echo "Running: partial value addition/subtraction tests..."
${BUILD_DIR}/das1_fxtime 2005-01-01T12:30:59.375 -y 1.5 +j 182.5 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2005-01-01T12:30:59.375 -j 366.5 +h 12 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2005-01-01T12:30:59.375 -h 8784.5 +m 30 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2005-01-01T12:30:59.375 -m 527040.5 +s 30 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2005-01-01T12:30:59.375 -s 31622400.5 +s 0.5 >> ${BUILD_DIR}/das1_fxtime_test.txt
# half year stuff
${BUILD_DIR}/das1_fxtime 2005-182T12:00:00.000 -y 1 >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2005-182T12:00:00.000 -y 1.5 >> ${BUILD_DIR}/das1_fxtime_test.txt

# diff tests
echo "Running: difference tests..."
${BUILD_DIR}/das1_fxtime 2001-01-01 2001-01-02 -diff >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2000-12-31 2001-01-01 -diff >> ${BUILD_DIR}/das1_fxtime_test.txt
${BUILD_DIR}/das1_fxtime 2001-01-01 2001-01-02 >> ${BUILD_DIR}/das1_fxtime_test.txt

echo "   exec: diff test/das1_fxtime_output.txt ${BUILD_DIR}/das1_fxtime_test.txt"
if ${DIFFCMD} test/das1_fxtime_output.txt ${BUILD_DIR}/das1_fxtime_test.txt > /dev/null ; then 
	echo " Result: PASSED" 
	exit 0
else 
	echo " Result: FAILED, ${BUILD_DIR}/fxtime_test.txt not equal to test/das1_fxtime_output.txt"
	exit 4
fi
