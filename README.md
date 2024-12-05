# Re-SC-Masker

Effectively prevents secret leakage through statistical attacks on intermediate variables in cryptographic functions by employing perfect masking.

**NOTE: WORK IN PROGRESS.**

## Usage

```bash
# sudo apt install llvm llvm-dev clang clang-tools cmake ninja-build
mkdir build
cd ./build
cmake ..
make
# ./Re-SC-Masker ../input/minimum.cpp > ../output/minimum.cpp
```
## Examples

Locate in `/input`(original programs) and `/output`(masked programs).

- `minimum.cpp`: functional equivalence verified
- `tiny-size.cpp`: about 20 lines
- `medium-size.cpp`: about 50 lines

## Issues/TODOs

- Allow multiple uses of a variable!
- Automatically assign new names for temps
- More comments!
- Consistence naming (no more `lowerCamelCase`)
- Batch handling
- Reuse random/uniformly-distributed variables
- Non-trival classes

## Declaration

The algorithm is based on the original ScMasker tool introduced in *Hassan Eldib and Chao Wang. (2014). Synthesis of Masking Countermeasures against Side Channel Attacks*.