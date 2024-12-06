#!/bin/bash

input_dir="./input"
output_dir="./output"

mkdir -p "$output_dir"

for input_file in "$input_dir"/*.cpp; do
    filename=$(basename "$input_file")
    
    output_file="$output_dir/$filename"

    ./build/Re-SC-Masker "$input_file" > "$output_file"
    
    echo "Processed $input_file -> $output_file"
done