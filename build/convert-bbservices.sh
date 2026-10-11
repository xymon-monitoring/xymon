#!/bin/sh

# Script to convert a bbgen v3 protocols.cfg file to the
# [NAME] section delimited format used in Xymon v4 (starting with RC3).

FN="$1"

if test ! -r "${FN}"; then exit 0; fi

sed -e 's/^service \(.*\)/\[\1\]/' < "${FN}" >"${FN}.converted"
cmp -s "${FN}" "${FN}.converted"
if test $? -eq 0; then rm -f "${FN}.converted"; exit 0; fi

if test -f "${FN}.v3"; then rm -f "${FN}.v3"; fi
mv "${FN}" "${FN}.v3"
mv "${FN}.converted" "${FN}"
exit 0

