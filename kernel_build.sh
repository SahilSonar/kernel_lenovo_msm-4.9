#!/bin/bash
root_dir=`pwd`
cd ${root_dir} && cd ../../
kernel_dir=`pwd` && cd ${root_dir}
echo "kernel_dir=$kernel_dir"

mkdir out
export ARCH=arm64
export SUBARCH=arm64
export TARGET_ARCH_VARIANT=armv8-a
export TARGET_ARCH=arm64
export CROSS_COMPILE=${kernel_dir}/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
make ARCH=arm64 O=out msm8953-perf_defconfig
status=${PIPESTATUS[0]}
if [ "$status" != "0" ]; then
     echo "ERROR:  make defconfig  error:"
     exit
else
     echo "#### make defconfig successfully ####"
fi
make ARCH=arm64 -j8 O=out
re=${PIPESTATUS[0]}
if [ "$re" != "0" ];then
     echo "ERROR:  build kernel error:"
     exit
else
    echo "#### build kernel completed successfully ####"
fi



