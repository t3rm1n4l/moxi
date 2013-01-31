#/bin/bash
# Description: Build moxi rpm

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
