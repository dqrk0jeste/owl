#!/bin/bash

owl-ipc | while read -r line; do
  if [[ "$line" == active-toplevel\$* ]]; then
    title=$(echo "$line" | cut -d'$' -f3)
    echo "$title"
  fi
done
