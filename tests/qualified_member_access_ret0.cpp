// Test: qualified member access through base class (obj.Base::member())
// Pattern used in libstdc++ hashtable_policy.h: __x.__ebo_hash::_M_get()

struct Base {
    int val;
    int get_val() { return val; }
};

struct Derived : Base {
    using __base = Base;
    
    // Access member through base class qualifier on another object
    int read_other(Derived& other) {
        return other.__base::get_val();
    }
};

int main() {
    Derived a;
    a.val = 42;
    Derived b;
    b.val = 0;
    // b.read_other(a) should return 42 via other.__base::get_val()
    return b.read_other(a) == 42 ? 0 : 1;
}
