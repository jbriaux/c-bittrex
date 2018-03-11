#!/bin/bash

BBIN="./bittrex"
EXMARKET="BTC-XVG"
LOGDIR="/tmp/"

run_test() {
	local START=$(date +%s)
	local testnum=$1
        local message=$2

        echo "test $testnum: $message"
	# test are run in a subshell as we want the test suite to continue in
	# case of a test failure
        ( test_${testnum} || error "test_$testnum failed with $?" )
	duration=$(($(date +%s) - $START))

	echo "Test $1 passed, duration: $duration second(s)"
        return 0
}

error() {
    echo "$@"
    exit 1;
}

[ -x $BBIN ] || error "File: $BBIN not found or not executable"

#
# PUBLIC Calls tests (test 1 to 10)
#

test_1() {
    $BBIN --getmarkets  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    return $?
}

run_test 1 "getmarkets"

test_2() {
    $BBIN --getcurrencies  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    return $?
}

run_test 2 "getcurrencies"

test_3() {
    $BBIN --getmarketsummaries  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    return $?
}

run_test 3 "getmarketsumaries"

test_4() {
    $BBIN --market=$EXMARKET --getorderbook both  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    return $?
}

run_test 4 "getorderbook both"

test_5() {
    $BBIN --market=$EXMARKET --getorderbook buy  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    return $?
}

run_test 5 "getorderbook buy"

test_6() {
    $BBIN --market=$EXMARKET --getorderbook sell  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    return $?
}

run_test 6 "getorderbook sell"

test_7() {
    local -a type=(oneMin fiveMin thirtyMin Hour)
    for i in "${type[@]}" ; do
	$BBIN --market=$EXMARKET --getticks $i  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1 || error "getticks $i failed"
    done
    return 0
}

run_test 7 "getticks (oneMin fiveMin thirtyMin Hour)"

test_8() {
    $BBIN --market=$EXMARKET --getticker  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    return $?
}

run_test 8 "getticker"

test_9() {
    $BBIN --market=$EXMARKET --getmarketsummary  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    return $?
}

run_test 9 "getmarketsummary"

test_10() {
    $BBIN --market=$EXMARKET --getmarkethistory  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    return $?
}

run_test 10 "getmarkethistory"

#
# test expected to fail (bad args): 11 to 20
#

test_11() {
    $BBIN --nonexistingopt  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    (( $? != 0 )) || error "test expected to fail but did not"
}

run_test 11 "Call with non existing option"


#  test of --getticker||--getmarketsummary||--getmarkethistory with bad market option
test_12() {
    $BBIN --market=badmarket --getticker  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    (( $? != 0 )) || error "test expected to fail (non existing market) but did not"
}

run_test 12 "getticker with non existing marketname"

test_13() {
    $BBIN --market=badmarket --getmarketsummary  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    (( $? != 0 )) || error "test expected to fail (non existing market) but did not"
}

run_test 13 "getmarketsummary with non existing marketname"

test_14() {
    $BBIN --market=badmarket --getmarkethistory  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    (( $? != 0 )) || error "test expected to fail (non existing market) but did not"
}

run_test 14 "getmarkethistory with non existing marketname"

test_15() {
    $BBIN --market=$EXMARKET --getticks fortytwoMin  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    (( $? != 0 )) || error "test expected to fail (getticks with non existing interval) but did not"
}

run_test 15 "getticks with non existing interval"

test_16() {
    $BBIN --market=badmarket --getticks oneMin  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    (( $? != 0 )) || error "test expected to fail (getticks with bad market name) but did not"
}

run_test 16 "getticks with non existing market name"

test_17() {
    $BBIN --market=badmarket --getticks badinterval  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    (( $? != 0 )) || error "test expected to fail (getticks with bad market name and bad interval) but did not"
}

run_test 17 "getticks with non existing market name and non existing interval"

test_18() {
    $BBIN --market=$EXMARKET --getorderbook badorderbooktype  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    (( $? != 0 )) || error "test expected to fail (getorderbook with non existing interval) but did not"
}

run_test 18 "getorderbook with non existing orderbooktype"

test_19() {
    $BBIN --market=badmarket --getorderbook buy  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    (( $? != 0 )) || error "test expected to fail (getorderbook with non existing marketname) but did not"
}

run_test 19 "getorderbook with non existing marketname"

test_20() {
    $BBIN --market=badmarket --getorderbook badtype  > $LOGDIR"test_log.${FUNCNAME[0]}.log" 2>&1
    (( $? != 0 )) || error "test expected to fail (getorderbook with non existing marketname and bad orderbooktype) but did not"
}

run_test 20 "getorderbook with non existing marketname"

