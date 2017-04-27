#!/bin/sh
echo JPEG_LIB_VERSION | cpp -include jpeglib.h | tail -1
