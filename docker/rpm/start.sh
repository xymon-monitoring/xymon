#!/bin/sh

cd xymon

# build with minimal deps
./configure --server || exit $?
make || exit $?

yum -y install c-ares-devel openldap-devel rrdtool-devel openssl-devel libtirpc-devel || exit $?

# build with full deps
./configure --server || exit $?
make || exit $?
