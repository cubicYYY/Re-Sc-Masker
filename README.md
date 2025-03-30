# Re-SC-Masker

Effectively prevents secret leakage through statistical attacks on intermediate variables in cryptographic functions by employing perfect masking.

**NOTE: WORK IN PROGRESS.**

## Usage

```bash
# Read the official manual to install Z3 library first.
# https://github.com/Z3Prover/z3/blob/master/README.md#building-z3-using-cmake

sudo apt install llvm llvm-dev clang clang-tools cmake ninja-build
mkdir build
cd ./build
cmake ..
make

# Verify it:
./Re-SC-Masker ../input/minimum.cpp > ../output/minimum.cpp
```

## Limitations

- Must be inSSA format: no re-assignment to the same variable
- No constants are allowed currently (you can replace it with a public parameter)

## Examples

Locate in `/input`(original programs) and `/output`(masked programs).

- `minimum.cpp`: functional equivalence verified
- `tiny-size.cpp`: about 20 lines
- `medium-size.cpp`: about 50 lines

## Issues

- Unused local variables are problematic. You need to remove them.

## TODOs

### Deployment

- Docker

### Functionalities

- Support the move operation `a=b;`
- Update SymbolTable while issuing a new instruction (instead of manual update)
- Automatically assign new names for temps
- More comments!
- Consistence naming (no more `lowerCamelCase`)
- Reuse random/uniformly-distributed variables
- Non-trival classes
- Use Clang's declaration instead of class `Instruction`
- `return` handled as a normal instruction
- Less copying!

## Declaration

The algorithm is based on the original ScMasker tool introduced in *Hassan Eldib and Chao Wang. (2014). Synthesis of Masking Countermeasures against Side Channel Attacks*.