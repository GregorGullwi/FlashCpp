# ELFIO - ELF (Executable and Linkable Format) I/O Library

This directory contains the ELFIO library headers for ELF file manipulation.

## Source
ELFIO is a header-only C++ library for reading and generating files in ELF binary format.
- **Repository**: https://github.com/serge1/ELFIO
- **License**: MIT (see LICENSE.txt)
- **Version**: 3.x (latest stable)

## Files Included
Only the essential header files from the `elfio/` subdirectory are included:
- `elfio.hpp` - Main header
- `elf_types.hpp` - ELF type definitions
- `elfio_*.hpp` - Component headers (sections, symbols, relocations, etc.)

## Usage
```cpp
#include "elfio/elfio.hpp"
using namespace ELFIO;
```

## Maintainer Notes
This is a minimal inclusion of ELFIO headers only (no examples, tests, or documentation).
The full repository is not included to keep the FlashCpp codebase lean.
