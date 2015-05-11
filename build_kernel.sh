#!/bin/bash

export ARCH=arm
export CROSS_COMPILE=/home/duki994/arm-eabi-4.8/bin/arm-eabi-

make clean
make exynos5422-k3g_00_defconfig
make -j4
