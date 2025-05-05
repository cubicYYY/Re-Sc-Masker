# Re-SC-Masker

Effectively prevents secret leakage through statistical attacks on intermediate variables in cryptographic functions by employing perfect masking.

**NOTE: WORK IN PROGRESS.**

## Usage

```bash
# Read the official manual to install Z3 library first.
# https://github.com/Z3Prover/z3/blob/master/README.md#building-z3-using-cmake

sudo apt install llvm llvm-dev clang clang-tools cmake ninja-build
mkdir build 
cmake -B build
cmake --build build

# Verify it:
build/Re-Sc-Masker input/minimum.cpp > output/minimum.cpp
# Or:
# cmake --build build --config Debug
```

## Limitations

- No constants are allowed currently (you can replace it with a public parameter)
- Do not support branches (`if`)
- TODO

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

## Dev Specification

- Commits: [Conventional Commits](https://www.conventionalcommits.org/zh-hans/v1.0.0/)
- Formatting: always run `run_clang_format.sh` before committing
- Naming:
    - Variables: `snake_case`
    - Classes: `BigCamelCase`
    - Functions: `smallCamelCase`

## Implementation Details

## Declaration

The algorithm is based on the original ScMasker tool introduced in *Hassan Eldib and Chao Wang. (2014). Synthesis of Masking Countermeasures against Side Channel Attacks*.