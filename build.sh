#!/bin/bash
# Decription: Build moxi binary

PREFIX="$1"
if [ -z $PREFIX ];
then
   echo Usage: $0 prefix_path
   echo Missing prefix path
   exit 1
fi

SOURCE="$(pwd)/build/SOURCES"
mkdir -p $PREFIX
mkdir -p $SOURCE

if [ ! -e "$SOURCE/libevent-2.0.16-stable" ];
then
    echo Building libevent...
    (cd $SOURCE && wget --no-check-certificate https://github.com/downloads/libevent/libevent/libevent-2.0.16-stable.tar.gz && \
        tar -xvf libevent-2.0.16-stable.tar.gz -C $SOURCE )
    (cd $SOURCE/libevent-2.0.16-stable && ./configure --prefix=$PREFIX && make install)
fi

if [ ! -e "$SOURCE/libvbucket" ];
then
    echo Building libvbucket...
    mkdir -p $SOURCE/libvbucket
    git clone git://github.com/membase/libvbucket.git $SOURCE/libvbucket
    (cd $SOURCE/libvbucket && ./config/autorun.sh && ./configure --prefix=$PREFIX && make install)
fi

if [ ! -e "$SOURCE/libmemcached" ];
then
   echo Building libmemcached
   mkdir -p $SOURCE/libmemcached
   git clone git@github-ca.corp.zynga.com:membase/libmemcached.git $SOURCE/libmemcached
   (cd $SOURCE/libmemcached && ./config/autorun.sh && ./configure --prefix=$PREFIX --without-memcached && make install)
fi

echo Building moxi...
./config/autorun.sh && ./configure --enable-moxi-libvbucket --enable-moxi-libmemcached --without-check --no-recursion --without-memcached --without-libvbucket-prefix --with-libvbucket-prefix=$PREFIX --without-libevent-prefix --with-libevent-prefix=$PREFIX --without-libhashkit-prefix --with-libhashkit-prefix=$PREFIX --prefix=$PREFIX && make install

/sbin/ldconfig -n $PREFIX/lib
