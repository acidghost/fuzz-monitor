#!/usr/bin/env bash

bold=`tput bold`
normal=`tput sgr0`

function usage {
  echo -e <<USAGE "${bold}Find basic blocks in a binary using radare2${normal}\n
  usage: `basename $0` [options] binary\n
  options:  -h  prints this help
            -x  prints addresses in hexadecimal format\n
  output: one row per basic block,
          columns: starting addr., ending addr. and size"
USAGE
  exit 1
}

hex_format=false
while getopts "xh", opt; do
  case "${opt}" in
    x)
      hex_format=true
      ;;
    *|h)
      usage
      ;;
  esac
done

shift $((OPTIND-1))
[ ! -e "$1" ] && exit 1

r2out=`r2 -q0 -A -c 'afbj @@f' $1 | \
	jq '.[] | "\(.addr) \(.addr + .size) \(.size)"' | \
	sed s/\"//g`

if [ $hex_format = true ]; then
  echo "$r2out" | awk '{printf("0x%x 0x%x %d\n", $1, $2, $3)}'
else
  echo "$r2out"
fi
