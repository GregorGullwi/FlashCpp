// Bug: Class Template Argument Deduction (CTAD) produces link errors
// Status: LINK ERROR - Template not instantiated for deduced type
// Date: 2026-02-07
//
// CTAD (C++17) should allow constructing template classes without explicit
// template arguments when the type can be deduced from constructor arguments.

template<typename T>
class Box {
public:
    T value;
    Box(T v) : value(v) {}
    T get() const { return value; }
};

int main() {
    Box box(42);  // Should deduce Box<int>
    return box.get() == 42 ? 0 : 1;
}

// Expected behavior (with clang++/g++):
// Compiles and runs successfully, returns 0
//
// Actual behavior (with FlashCpp):
// Compiles successfully but linking fails with:
//   undefined reference to `Box::Box(T)'
//   undefined reference to `Box::get()'
// The compiler fails to instantiate the template with the deduced type.
//
// Workaround: Use explicit template arguments: Box<int> box(42);
//
// Fix: Implement CTAD deduction guides or at least basic constructor
// argument deduction to determine the template parameter type and
// trigger proper template instantiation.
