#!/bin/bash

mkdir merge
find . -name '*.a' -exec cp "{}" merge \;

SCRIPT=$'CREATE merge/librockchip_mpp_merged.a\n';
for LIB in merge/*.a
do
    SCRIPT="$SCRIPT""ADDLIB "$LIB$'\n'
done

SCRIPT="$SCRIPT"$'SAVE\nEND\n'

echo "$SCRIPT" > script.ar

ar -M < script.ar

rm mpp/librockchip_mpp.a
cp merge/librockchip_mpp_merged.a mpp/librockchip_mpp.a
rm script.ar
rm -rf merge