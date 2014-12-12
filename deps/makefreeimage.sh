#!/usr/bin/env bash

cd $(dirname $0)

if [ ! -d "FreeImage" ]; then
  echo false
  exit
fi

make -C  ./FreeImage > /dev/null 2>&1 & echo true
