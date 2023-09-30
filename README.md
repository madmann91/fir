# Fir

Fir is a functional IR with a graph-based design written completely in C. A particularity of this IR
is that it supports both continuation-passing-style (CPS) and direct-style (DS) for function calls,
letting the client choose the best mode depending on the situation. A typical use-case for this
feature is to encode control-flow inside a function in CPS and regular function calls in DS, taking
advantage the strengths of both.

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

This project is distributed under the [MIT license](LICENSE.txt).
