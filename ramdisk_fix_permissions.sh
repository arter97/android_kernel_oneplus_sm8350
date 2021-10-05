#!/bin/bash

find . -type f -exec chmod 644 {} +
find . -type d -exec chmod 755 {} +

find */bin */*/bin -type f -exec chmod 755 {} +

chmod 555 config
chmod 644 file_contexts
chmod 644 se*
chmod 644 *.sh
chmod 644 *.rc
chmod 750 init*
chmod 640 fstab*
chmod 771 data
chmod 777 sdcard
chmod 755 dev
chmod 755 proc
chmod 755 bin
chmod 755 bin/*
chmod 755 sbin
chmod 750 sbin/*
chmod 751 storage
chmod 755 sys
chmod 755 system
chmod 751 system/bin
chmod 550 system/bin/logd
