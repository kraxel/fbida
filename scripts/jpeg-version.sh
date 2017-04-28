#!/bin/sh
echo JPEG_LIB_VERSION | cpp $CFLAGS -include jpeglib.h | tail -1
