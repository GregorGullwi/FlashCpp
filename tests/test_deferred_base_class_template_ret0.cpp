// Test deferred base class resolution in template bodies
// Pattern from _Hashtable: using __hash_code_base = ...; struct Derived : __hash_code_base {}
template<typename T>
struct Outer {
    using inner_base = T;
    
    struct InnerDerived : inner_base {
        int extra;
    };
};

struct ConcreteBase {
    int base_val;
};

int main() {
    Outer<ConcreteBase>::InnerDerived d;
    d.extra = 42;
    (void)d;
    return 0;
}
