# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

## Range-for with inline struct iterator member functions

Range-for loops using struct iterators with inline member function definitions
(operator*, operator++, operator!=) crash at runtime (signal 11). Out-of-line
definitions work correctly. See `tests/test_range_for_auto_struct_iterator_ret0.cpp`
for a working pattern.
