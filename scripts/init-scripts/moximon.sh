#! /bin/bash 
# Description:  moxi monitor script

#   Copyright 2013 Zynga Inc.
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.

# Source function library.
. /etc/rc.d/init.d/functions

USER=nobody
PIDFILE="/var/run/moxi/moxi.pid"
MAXCONN=10000
PORT=11213
THREADS=2
SOCKET="/var/run/moxi/moxi.sock"
CONNS_PER_MEMCACHED=8
PERMS=766
NUM_FAILURES=20
RETRY_TIMEOUT=30
OPTIONS=""
MAX_MEMORY=256

# spit everything to syslog using logger
exec &> >(logger -t '[moxi]' --)

# keep moxi running  continuously
while :; do
    if [ -f /etc/sysconfig/moxi ];then
        . /etc/sysconfig/moxi
    fi
    daemon /opt/moxi/bin/moxi -d -X -u $USER -c $MAXCONN -t $THREADS -s $SOCKET -a $PERMS -P $PIDFILE -m $MAX_MEMORY $OPTIONS -Z downstream_conn_max=$CONNS_PER_MEMCACHED -v
    RETVAL=$?
    if [ $RETVAL -ne 0 ];then
        echo $RETVAL
        exit 0
    fi

    while :; do
        sleep 10
        if [[ ! -f "$PIDFILE" ]];then   # moxi has been manually stopped
            continue
        fi
        read pid < "$PIDFILE"
        if [[ -n "$pid" && -d "/proc/$pid" ]];then
            continue
        else    # moxi is not running, restart it
            break
        fi
    done
done

