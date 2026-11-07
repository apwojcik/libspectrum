#!/bin/bash
echo -n "Verify cmake line '$1' .. "
lines1=(`cat "./cmake.out" | grep -c "$1"`)
if [[ "$lines1" -lt "1" ]]; then
  echo "NOT FOUND"
  exit 1
fi
echo "OK"
