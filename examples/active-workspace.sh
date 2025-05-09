#!/bin/bash

/home/darko/projects/mwc/build/mwc-ipc watch active_workspace | while read -r -d '' line; do
    index=$(echo "$line" | jq -r '.index')
    echo "$index"
done
