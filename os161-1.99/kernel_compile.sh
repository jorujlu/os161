#!/bin/bash

cd os161-1.99
./configure --ostree=$HOME/cs350-os161/root --toolprefix=cs350-
cd kern/conf
./config ASST0
cd ../compile/ASST0
bmake depend
sleep "2"
bmake
sleep "2"
bmake install
