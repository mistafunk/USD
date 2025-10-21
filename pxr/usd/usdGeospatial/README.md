# Prototype Implementation of a Geospatial library

This implementation exists to experiment with concepts [discussed](https://docs.google.com/document/d/1v9A5SCSz_yvoExFgb9kJ5qPo4CAAGZtXnvS2ZptfvXc/edit?usp=sharing) in the [AOUSD AECO Interest Group](https://aousd.org/community/interest-groups/) for a future geospatial support library.

## Building

In this branch, the new `usdGeospatial` library is hardcoded into the build system, i.e.  see the [main documentation](/BUILDING.md) regarding building OpenUSD. The geospatial experiments are implemented as [tests](./testenv), so make sure to build OpenUSD with [test support](/BUILDING.md#tests).

Additional Requirement:  the `UsdGeospatialBindingAPI` class is dependent on the [Proj](https://proj.org/en/stable/install.html) library, make sure it is installed on the System and visible to CMake's 'find_package'.

## Running the tests/experiments

Change into your CMake build directory and run:
`ctest -R testUsdGeospatial -V`
