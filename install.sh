#!/bin/sh
#
# Modified from https://github.com/kupiqu/SierraBreezeEnhanced
# - Recreate build
# - Build
# - Install

ORIGINAL_DIR=$(pwd)

mkdir -p build
cmake -DCMAKE_INSTALL_PREFIX=/usr -B build
make -C build -j12
sudo make -C build DESTDIR="${pkgdir}" PREFIX=/usr install

cd $ORIGINAL_DIR
