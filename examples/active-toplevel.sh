#!/bin/bash

# active-toplevel event has two args separated by the \x1E separator sequence
#   - toplevel class
#   - toplevel title

owl-ipc | while read -r line; do
  # if the line starts with active-toplevel
  if [[ "$line" == active-toplevel* ]]; then
    # we extract the arguments and take the third one - title
    title=$(echo "$line" | cut -d$(printf '\x1E') -f3)
    echo "$title"
  fi
done
