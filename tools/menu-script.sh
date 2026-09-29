#!/bin/bash
# menu-script.sh BRIEF ACCEPT1 ACCEPT2: the mission-1 menu path with the given click steps
b=$1; a1=$2; a2=$3
cat <<EOT
key 22 44 1
key 24 44 0
key 72 59 1
key 74 59 0
mouse $((b-2)) 42 179
button $b left 1
button $((b+2)) left 0
button $a1 left 1
button $((a1+2)) left 0
button $a2 left 1
button $((a2+2)) left 0
EOT
