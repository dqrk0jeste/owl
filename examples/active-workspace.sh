#!/bin/bash

owl-ipc | while read -r line; do
  if [[ "$line" == active-workspace\$* ]]; then
    number=$(echo "$line" | cut -d'$' -f2)
    echo "workspace $number"
  fi
done
