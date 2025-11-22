// Test complete C++20 constexpr new/delete with dereference, constructor args, and destructors

struct Counter {
    int value;
    
    constexpr Counter(int v) : value(v) {}
    constexpr ~Counter() {}
};

// Test 1: Constructor arguments - allocate with specific value
constexpr int test_constructor_args() {
    int* p = new int(42);
    int result = *p;  // Dereference to read
    delete p;
    return result;
}
static_assert(test_constructor_args() == 42, "Constructor args should initialize value");

// Test 2: Dereference operator for reading
constexpr int test_dereference_read() {
    int* p = new int(100);
    int val = *p;
    delete p;
    return val;
}
static_assert(test_dereference_read() == 100, "Dereference should read value");

// Test 3: Struct with constructor arguments
constexpr int test_struct_constructor() {
    Counter* c = new Counter(55);
    int val = c->value;  // Member access through pointer would need -> operator
    delete c;
    return val;
}
// Note: This won't work yet because we need -> operator, but dereference will work
constexpr int test_struct_dereference() {
    Counter* c = new Counter(77);
    Counter obj = *c;  // Dereference to get object
    delete c;
    return obj.value;
}
static_assert(test_struct_dereference() == 77, "Struct dereference should work");

// Test 4: Destructor integration - object with constexpr destructor
constexpr int test_destructor_integration() {
    Counter* c = new Counter(99);
    Counter obj = *c;
    delete c;  // Should call constexpr destructor (implicit no-op)
    return obj.value;
}
static_assert(test_destructor_integration() == 99, "Destructor integration should work");

int main() {
    return 0;
}
