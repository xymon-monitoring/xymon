#!/bin/sh

# Separate $CFLAGS and $CPPFLAGS for c-ares, which is now a bit more strict about things
# 
# This will be moot if we get around to autoconfiscating the build process.
# 

for i in $CFLAGS; do
   if [ `echo $i | grep -c '\-I'` -eq 0 -a `echo $i | grep -c '\-D'` -eq 0 -a `echo $i | grep -c '\-L'` -eq 0 ]; then
        CFF="$CFF $i"
   elif [ `echo $i | grep -c '\-L'` -eq 0 ]; then
        CPF="$CPF $i"
   else
        echo "REJECTING '${i}' in CFLAGS"
   fi
done

CFLAGS="$CFF" CPPFLAGS="$CPF" $*

