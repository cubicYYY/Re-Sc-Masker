#!/bin/bash
# Run this batch script to mask all the input files in the input_dir.
# The masked files will be saved in the output_dir.

input_dir="./input"
output_dir="./output"

mkdir -p "$output_dir"

for input_file in "$input_dir"/*.cpp; do
    filename=$(basename "$input_file")
    
    output_file="$output_dir/$filename"

    ./build/Re-Sc-Masker "$input_file" > "$output_file"
    
    echo "Processed $input_file -> $output_file"
done