#!/bin/sh
cat /usr/include/xkbcommon/xkbcommon-keysyms.h \
	| awk '{ print $2 }' \
	| grep -e '^XKB_KEY_' \
	| while read key; do
printf '{ .code = %-24s, .name = "%s" },\n' "${key}" "${key#XKB_KEY_}"
done > xkbname.h
