![Logo](logo.svg)

![Workflow status](https://github.com/madmann91/fir/actions/workflows/build-test-action.yml/badge.svg)

# Fir

Fir is a functional intermediate represention (IR) with a graph-based design written completely in
C. Here are a few features that differentiate this IR from similar projects:

- It is entirely graph-based. In particular, instruction ordering is determined automatically via a
  scheduling algorithm based on C. Click's "Global Code Motion/Global Value Numbering" article. The
  algorithm used in this IR is an extension that does not generate partially-dead code and treats
  operations with side-effects correctly.
- Functions and basic-blocks are both encoded as functions in the IR, simplifying the implementation
  and making optimizations more aggressive. For instance, some optimizations that apply to functions
  can also be applied to basic-blocks without requiring any change. Calls and jumps are essentially
  the same in the IR, only the return type determines whether the called function is a basic block
  or not.
- The IR supports functional programs: Functions are first-class citizens, meaning that they can be
  passed as values to other functions. Closure conversion happens at the IR level and is responsible
  for lowering any higher-order functions to first-order ones.
- Due to being written in C, this project is very lightweight both at compile- and run-time. The
  entire project compiles in around 5s on a Ryzen 2700U from 2019.
- A small language (ash) that showcases the features of the IR is provided for testing and as an
  example of how to use the API.

This project is inspired by [Thorin](https://github.com/AnyDSL/thorin), a project I used to worked
on, and by [libfirm](https://github.com/libfirm/libfirm) another compiler IR in C.

## Building

This project requires a C23-compliant compiler and CMake. Optionally, it may use PCRE2 if it detects
it for the unit test suite. The following commands build the project:

    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=<Debug|Release> ..
    make

## Testing

Once built, the project can be tested via:

    make test

or, alternatively, when trying to debug memory leaks or other memory errors with a memory checker:

    make memcheck

## Documentation

The project supports doxygen for the publicly visible API. This can be generated either by calling
doxygen from the `doc` directory or by typing `make doc` using the CMake-generated Makefile (see
instructions above to build using CMake).

## License

This project is distributed under the [GPLv3](LICENSE.txt).
