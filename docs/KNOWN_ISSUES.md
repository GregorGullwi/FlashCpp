# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

- Indirect virtual base construction/layout is still incorrect when the most-derived
  class inherits a virtual base only through an intermediate base. A minimal repro is:
  `struct V { int v; V(int x = 7) : v(x) {} }; struct B : virtual V { B() : V(23) {} }; struct D : B { D() : V(11), B() {} };`
  `D{}.v` currently observes the intermediate-base initialization instead of the
  most-derived virtual-base initialization.
