#! /bin/bash 
# Description:  moxi monitor script

# Source function library.
. /etc/rc.d/init.d/functions

USER=nobody
PIDFILE="/var/run/moxi/moxi.pid"
MAXCONN=10000
PORT=11213
THREADS=2
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
    daemon /opt/moxi/bin/moxi -d -X -u $USER -c $MAXCONN -t $THREADS -p $PORT -a $PERMS -P $PIDFILE -m $MAX_MEMORY $OPTIONS -Z downstream_conn_max=$CONNS_PER_MEMCACHED -v
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

