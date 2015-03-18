#!/bin/bash

cd os161-1.99
./configure --ostree=$HOME/cs350-os161/root --toolprefix=cs350-
cd kern/conf
./config ASST2
cd ../compile/ASST2
bmake depend
bmake
bmake install
