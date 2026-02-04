#!/bin/sh

cd xymon

# build with minimal deps
./configure --server || exit $?
make || exit $?

apt-get install libc-ares-dev libldap-dev librrd-dev libssl-dev libtirpc-dev || exit $?

# build with full deps
./configure --server || exit $?
make || exit $?
