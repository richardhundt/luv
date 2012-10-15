#!/bin/sh
rm -fr build
mkdir build
(cd build && cmake -D USE_ZMQ=1 .. && make)
