#!/bin/bash

owl-ipc | while read -r line; do
  number=$(echo "$line" | awk '$1 == "active-workspace" {print $2}')

  if [[ -n $number ]]; then
    echo $number
  fi
done
