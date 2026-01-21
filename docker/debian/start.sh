#!/bin/sh

pwd
ls -l

cd xymon

./configure --server || exit $?
make || exit $?
