#!/usr/bin/env bash

bin=$1
sect=$2

if [ ! -e "$bin" ]; then
	echo "Binary" $bin "does not exist"
	exit 1
fi

test -z "$sect" && sect=".text"
# echo "Looking for section" $sect

line=(`objdump -h -j $sect $bin 2> /dev/null | awk -v sect="$sect" '$2 ~ sect { printf("%s %s %s", $2, $4, $3) }'`)
if [ -z "$line" ]; then
	echo "No section named" $sect "was found"
	exit 1
fi

line[1]=$((16#${line[1]}))
line[3]=$((16#${line[2]}))
line[2]=$((${line[1]} + ${line[3]}))
echo ${line[*]}

hex_line=$line
for i in `seq 3`; do
	hex_line[$i]=`printf "0x%x" ${line[$i]}`
done
echo ${hex_line[*]}
