#!/usr/bin/env bash

m=$(cat)
echo -e $(printf '\\x01\\x00\\x%02x\\x00%s' $((1 + ${#m})) "$m") | nc $@ &> /dev/null
