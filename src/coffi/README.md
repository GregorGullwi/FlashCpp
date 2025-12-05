# COFFI - COFF (Common Object File Format) I/O Library

This directory contains the COFFI library headers for COFF/PE file manipulation.

## Source
COFFI is a header-only C++ library for reading and generating files in COFF/PE binary format.
- **Repository**: https://github.com/serge1/COFFI
- **License**: MIT (see LICENSE.txt)
- **Version**: 2.x (latest stable)

## Files Included
Only the essential header files from the `coffi/` subdirectory are included:
- `coffi.hpp` - Main header
- `coffi_types.hpp` - COFF type definitions
- `coffi_*.hpp` - Component headers (sections, symbols, relocations, etc.)

## Usage
```cpp
#include "coffi/coffi.hpp"
using namespace COFFI;
```

## Maintainer Notes
This is a minimal inclusion of COFFI headers only (no examples, tests, or documentation).
The full repository is not included to keep the FlashCpp codebase lean.
Both COFFI and ELFIO are from the same author (Serge Lamikhov-Center).
