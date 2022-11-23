#!/bin/bash
# on proxy side run without parameters.
# on mole side run ./auto_run_relay.sh 1


mole=$#
echo "$#"
if [[ "$mole" -eq 0 ]]; then
    echo 'this is proxy'
else
    echo 'this is mole'
fi

found=0
# program=$HOME/Documents/server/src/relay
program=$HOME/server/src/relay
while true
do
    pgrep -x relay >/dev/null && found=1 || found=0

    if [[ "$found" -eq 1 ]]; then
        echo 'OK'
    else
        echo 'relay program NOT found'
        sleep 6
        if [[ "$mole" -eq 0 ]]; then ########### proxy
            echo "RUN $program"
            $program &
        else ###########  mole
            pgrep -x relay >/dev/null && found=1 || found=0
            if [[ "$found" -eq 0 ]]; then
                if test -f "$program"; then
                    echo "$program exists."
                else
                    echo "NOT found $program"
                fi
                proxy_ip=$(ping -c1 -t1 -W0 proxypi.local | tr -d '():' | awk '/^PING/{print $3}')
                # proxy_ip=$(ping -c1 -t1 -W0 molepi.local | tr -d '():' | awk '/^PING/{print $3}')
                if [[ $proxy_ip =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
                    echo "RUN $program $proxy_ip:8000"
                    
                    $program $proxy_ip:8000 &
                else
                    echo "proxy_ip NOT found"
                    echo $proxy_ip
                fi
            fi    
        fi
    fi
    sleep 20
done
