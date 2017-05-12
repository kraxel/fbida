#!/bin/sh
prefix="$1"

for dir in \
	$prefix/*/X11/app-defaults		\
	$prefix/X11R*/*/X11/app-defaults	\
	/usr/*/X11/app-defaults			\
	/etc/app-defaults			\
; do
	if test -d "$dir"; then
		echo "$dir"
		exit 0
	fi
done
exit 1
