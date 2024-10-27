#!/bin/bash

owl-ipc | while read -r line; do
  title=$(echo "$line" | awk '$1 == "active-toplevel" {print $3}')

  printf "{\"text\": \"$title\", \"alt\": \"\", \"tooltip\": \"\", \"class\": \"\", \"percentage\": 0}\n"
done
