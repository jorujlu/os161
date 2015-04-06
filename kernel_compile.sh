#!/bin/bash

cd os161-1.99
./configure --ostree=$HOME/cs350-os161/root --toolprefix=cs350-
cd kern/conf
./config ASST3
cd ../compile/ASST3
bmake depend
bmake
bmake install
