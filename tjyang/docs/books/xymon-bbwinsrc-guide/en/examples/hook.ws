#!/bin/bash

hg init a
cd a
echo '[hooks]' > .hg/hgrc
echo "pretxncommit.whitespace = hg export tip | (! egrep -q '^\\+.*[ \\t]$')" >> .hg/hgrc

#$ name: simple

cat .hg/hgrc
echo 'a ' > a
hg commit -A -m 'test with trailing whitespace'
echo 'a' > a
hg commit -A -m 'drop trailing whitespace and try again'

#$ name:

echo '[hooks]' > .hg/hgrc
echo "pretxncommit.whitespace = .hg/check_whitespace.py" >> .hg/hgrc
cp $EXAMPLE_DIR/data/check_whitespace.py .hg

#$ name: better

cat .hg/hgrc
echo 'a ' >> a
hg commit -A -m 'add new line with trailing whitespace'
sed -i 's, *$,,' a
hg commit -A -m 'trimmed trailing whitespace'

#$ name:
exit 0
