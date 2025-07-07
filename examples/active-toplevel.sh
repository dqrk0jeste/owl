#!/bin/bash

/home/darko/projects/mwc/build/mwc-ipc watch focused_toplevel | while read -r -d '' line; do
    if [ "$line" == "null" ]; then
        echo ""
    else
        title=$(echo "$line" | jq -r '.title')
        echo "$title"
    fi
done
