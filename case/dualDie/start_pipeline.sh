#!/bin/bash

cd /opt/demo/pipeline/tools

#./devmem 0x518281c8 32 1
# NPU  set to 1G HZ
./devmem 0x51828180 32 0x80002112
./devmem 0x5182817c 32 0x80000820
./devmem 0x71828180 32 0x80002112
./devmem 0x7182817c 32 0x80000820

cd /opt/demo/pipeline/case/dualDie/die1

./clearvb.sh

sleep 1

./die1.sh &

sleep 10

cd  /opt/demo/pipeline/case/dualDie/die0

./die0.sh &

sleep 3

cd /home/eswin
