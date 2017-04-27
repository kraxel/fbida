#!/bin/sh
blob="$1"
chdr="$2"
hexdump -v -e '1/1 "0x%02x,"' < "$blob" > "$chdr"
