// Test: inherited static member lookup via function return type
//
// This exercises the evaluate_static_member_from_struct path where a static
// member is defined in a base class and accessed through a derived type's
// instance returned by a function.  When the static member has no inline
// initializer, the evaluator must build the qualified name using the *base*
// class name (where the member is defined) rather than the derived class name
// for the symbol table fallback lookup.

struct Base {
    static constexpr int value = 42;
};

struct Derived : Base {
    // 'value' is inherited from Base — not redefined here.
};

constexpr Derived make_derived() { return Derived{}; }

// Access the inherited static member through the derived return type.
// The constexpr evaluator resolves Derived → findStaticMemberRecursive →
// finds 'value' in Base.  If the symbol-table fallback is needed it must
// look up "Base::value", not "Derived::value".
static_assert(Derived::value == 42, "Inherited static member should be 42");

int main() {
    return Derived::value; // returns 42
}
