#!/bin/bash

mkdir bld/merge
find bld -name '*.a' -exec cp "{}" bld/merge \;

SCRIPT=$'CREATE bld/merge/librockchip_mpp_merged.a\n';
for LIB in bld/merge/*.a
do
    SCRIPT="$SCRIPT""ADDLIB "$LIB$'\n'
done

SCRIPT="$SCRIPT"$'SAVE\nEND\n'

echo "$SCRIPT" > script.ar

ar -M < script.ar

rm bld/mpp/librockchip_mpp.a
cp bld/merge/librockchip_mpp_merged.a bld/mpp/librockchip_mpp.a
rm script.ar
rm -rf bld/merge