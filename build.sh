#!/bin/bash
# Decription: Build moxi binary

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
    git clone git://github.com/zbase/libvbucket.git $SOURCE/libvbucket
    (cd $SOURCE/libvbucket && ./config/autorun.sh && ./configure --prefix=$PREFIX && make install)
fi

if [ ! -e "$SOURCE/libmemcached" ];
then
   echo Building libmemcached
   mkdir -p $SOURCE/libmemcached
   git clone git://github.com/zbase/libmemcached.git $SOURCE/libmemcached
   (cd $SOURCE/libmemcached && ./config/autorun.sh && ./configure --prefix=$PREFIX --without-memcached && make install)
fi

echo Building moxi...
./config/autorun.sh && ./configure --enable-moxi-libvbucket --enable-moxi-libmemcached --without-check --no-recursion --without-memcached --without-libvbucket-prefix --with-libvbucket-prefix=$PREFIX --without-libevent-prefix --with-libevent-prefix=$PREFIX --without-libhashkit-prefix --with-libhashkit-prefix=$PREFIX --prefix=$PREFIX && make install

/sbin/ldconfig -n $PREFIX/lib
