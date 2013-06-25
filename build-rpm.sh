#/bin/bash
# Description: Build moxi rpm

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

specfile=moxi-zynga.spec
prefix="$(pwd)/build/BUILD/opt/moxi/"
buildpath="$(pwd)/build/BUILD"
buildtmp="$(pwd)/build/"
topdir="$(pwd)/build/"
version=$(cat VERSION)
mkdir -p $topdir/{SRPMS,RPMS,BUILD,SOURCES}
if [ ! -e $prefix ];
then
    ./build.sh $prefix
fi

mkdir -p $buildpath/etc/init.d/
cp scripts/init-scripts/moxi $buildpath/etc/init.d/
cp scripts/init-scripts/moximon.sh $prefix
rpmbuild --define="version $version" --define="buildpath $buildtmp" --define="_topdir $topdir" -ba $specfile --buildroot $buildpath
