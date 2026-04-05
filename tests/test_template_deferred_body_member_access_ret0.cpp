// Test: template member function deferred body with unqualified member access,
// implicit 'this', and inherited member visibility.
// Exercises the template body re-parse path in Parser_Templates_Inst_ClassTemplate.cpp
// that now uses FunctionParsingScopeGuard for full member-function context.

struct Base {
    int base_val;
};

template<typename T>
struct Container : Base {
    T data_;
    int tag_;

    // Inline member accessing 'this' and other members
    T get_data() { return this->data_; }

    // Inline member using unqualified member name
    int get_tag() { return tag_; }

    // Member calling another member
    T get_via_call() { return get_data(); }

    // Member accessing inherited member
    int get_base() { return base_val; }
};

int main() {
    Container<int> c;
    c.data_ = 42;
    c.tag_ = 7;
    c.base_val = 100;

    if (c.get_data() != 42) return 1;
    if (c.get_tag() != 7) return 2;
    if (c.get_via_call() != 42) return 3;
    if (c.get_base() != 100) return 4;

    Container<long> c2;
    c2.data_ = 99;
    c2.tag_ = 3;
    c2.base_val = 200;

    if (c2.get_data() != 99) return 5;
    if (c2.get_tag() != 3) return 6;
    if (c2.get_via_call() != 99) return 7;
    if (c2.get_base() != 200) return 8;

    return 0;
}
