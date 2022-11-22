#!/bin/bash
STRING="UPLOAD PM3 TO Proxy PI."
echo $STRING

path="/Users/ab/Documents"
file="proxmark3-relay"
pi_path="/home/pi"
to="pi@proxypi.local"


rsync -avh --exclude '*.o' --exclude '*.d' --exclude '*.a' --exclude '*/obj/*' --exclude 'proxmark3-relay/*/obj/*' --exclude 'proxmark3-relay/client/proxmark3' --exclude 'proxmark3-relay/recovery/*' --exclude 'proxmark3-relay/tools/*'  --exclude 'proxmark3-relay/armsrc/fpga_version_info.c' $path/${file} $to:$pi_path
#rsync -avh $path/${file} $to:$pi_path
# ssh -t $to "cd ~/${file} && make clean && make"
ssh -t $to "cd ~/${file} && make"

file="server"
rsync -avh --exclude '*.o' $path/${file} $to:$pi_path
ssh -t $to "cd ~/${file}/src && mv ./auto_run_relay.sh ../../ && make clean && make"
# ssh -t $to "cd ~/${file}/src && make clean && make && $ ./relay"


