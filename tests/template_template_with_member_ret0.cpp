// Level 1: a normal class template that will be passed around
template<typename T>
struct Inner {
    static int val() { return 42; }
};

// Level 2: a template that takes a template<...> and operates on an instantiation of it
// (this is a "template-template" parameter)
template<template<typename> class TT>
struct Mid {
    using Inst = TT<int>;
    static int get() { return Inst::val(); }
};

// Level 3: a template that itself takes a template-template parameter.
// This is a "template-template-template" parameter: it accepts a template (TTT)
// whose parameter is a template<...>.
template<template<template<typename> class> class TTT>
struct Outer {
    // Instantiate TTT with Inner (a template<typename> class)
    using MidType = TTT<Inner>;
    static int call() { return MidType::get(); }
};

// A concrete TTT: it takes a template<...> (X) and produces a type that exposes get()
// (here it simply delegates to Mid<X>)
template<template<typename> class X>
struct MakeMid {
    static int get() { return Mid<X>::get(); }
};

int main() {
    // Outer receives MakeMid (a template that expects a template<typename> class),
    // MakeMid is instantiated with Inner inside Outer, which causes the chain:
    // Inner<int>::val() -> Mid<Inner>::get() -> MakeMid<Inner>::get() -> Outer<MakeMid>::call()
    const int result = Outer<MakeMid>::call();
    return result == 42 ? 0 : 1;
}