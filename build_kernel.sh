#!/bin/bash
export KERNELDIR=`readlink -f .`
export RAMFS_SOURCE=`readlink -f $KERNELDIR/ramdisk`
export PARTITION_SIZE=134217728

export OS="12.0.0"
export SPL="2022-01"

echo "kerneldir = $KERNELDIR"
echo "ramfs_source = $RAMFS_SOURCE"

RAMFS_TMP="/tmp/arter97-op9-ramdisk"

echo "ramfs_tmp = $RAMFS_TMP"
cd $KERNELDIR

if [[ "${1}" == "skip" ]] ; then
	echo "Skipping Compilation"
else
	echo "Compiling kernel"
	cp defconfig .config
	make "$@" || exit 1
fi

echo "Building new ramdisk"
#remove previous ramfs files
rm -rf '$RAMFS_TMP'*
rm -rf $RAMFS_TMP
rm -rf $RAMFS_TMP.cpio
#copy ramfs files to tmp directory
cp -axpP $RAMFS_SOURCE $RAMFS_TMP
cd $RAMFS_TMP

#clear git repositories in ramfs
find . -name .git -exec rm -rf {} \;
find . -name EMPTY_DIRECTORY -exec rm -rf {} \;

$KERNELDIR/ramdisk_fix_permissions.sh 2>/dev/null

cd $KERNELDIR
rm -rf $RAMFS_TMP/tmp/*

cd $RAMFS_TMP
find . | fakeroot cpio -H newc -o | lz4 -l > $RAMFS_TMP.cpio.lz4
ls -lh $RAMFS_TMP.cpio.lz4
cd $KERNELDIR

echo "Making new boot image"
mkbootimg \
    --kernel $KERNELDIR/arch/arm64/boot/Image.gz \
    --ramdisk $RAMFS_TMP.cpio.lz4 \
    --pagesize 4096 \
    --os_version     $OS \
    --os_patch_level $SPL \
    --header_version 3 \
    -o $KERNELDIR/boot.img

GENERATED_SIZE=$(stat -c %s boot.img)
if [[ $GENERATED_SIZE -gt $PARTITION_SIZE ]]; then
	echo "boot.img size larger than partition size!" 1>&2
	exit 1
fi

echo "done"
ls -al boot.img
echo ""
