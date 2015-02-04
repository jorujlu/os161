#!/bin/bash

cd os161-1.99
./configure --ostree=$HOME/cs350-os161/root --toolprefix=cs350-
cd kern/conf
./config ASST1
cd ../compile/ASST1
bmake depend
bmake
bmake install
