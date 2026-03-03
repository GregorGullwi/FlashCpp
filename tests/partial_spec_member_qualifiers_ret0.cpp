// Test: const member functions in member struct template partial specializations.
// Verifies that the const qualifier is correctly propagated during parsing
// so const member functions are callable on const references.

struct Wrapper {
    template<typename T>
    struct Box {
        T val;
        int get() const { return 10; }
    };

    // Partial specialization for pointers
    template<typename T>
    struct Box<T*> {
        T* val;
        int get() const { return 30; }
    };
};

int main() {
    int result = 0;

    // Primary template - const get() callable on const ref
    Wrapper::Box<int> b1{5};
    const Wrapper::Box<int>& cb1 = b1;
    if (cb1.get() != 10) result |= 1;
    if (b1.get() != 10) result |= 2;

    // Partial spec - const get() callable on const ref
    int x = 7;
    Wrapper::Box<int*> b2{&x};
    const Wrapper::Box<int*>& cb2 = b2;
    if (cb2.get() != 30) result |= 4;
    if (b2.get() != 30) result |= 8;

    return result;
}
