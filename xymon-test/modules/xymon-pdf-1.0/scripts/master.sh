#!/bin/sh

source config

# Just list here scripts to launch
#
sh $HOBBITREP/scripts/rrdcputil.sh >/dev/null 2>&1
sh $HOBBITREP/scripts/rrdload.sh >/dev/null 2>&1
sh $HOBBITREP/scripts/rrdmemory.sh >/dev/null 2>&1
sh $HOBBITREP/scripts/rrdnetio.sh >/dev/null 2>&1
sh $HOBBITREP/scripts/rrdwio.sh >/dev/null 2>&1
sh $HOBBITREP/scripts/rrdrq.sh >/dev/null 2>&1

exit 0
