// test_eh_struct_throw_ret0.cpp
// Regression test: throwing and catching class-type exceptions.
// Previously, class typeinfo was an all-zero stub (no vtable pointer, no name
// string), causing __gxx_personality_v0 to crash.  The catch variable was also
// bound to the wrong indirection level in both the by-reference and by-value
// catch paths.

int g_dtor_called = 0;
int g_value_caught = -1;

struct MyException {
    int value;
    ~MyException() { g_dtor_called++; }
};

// Test 1: catch by reference gets the correct exception value
int test_catch_ref() {
    try {
        throw MyException{42};
    } catch (MyException& e) {
        return e.value;
    }
    return -1;
}

// Test 2: catch by value gets the correct exception value
int test_catch_val() {
    try {
        throw MyException{99};
    } catch (MyException e) {
        return e.value;
    }
    return -1;
}

// Test 3: destructor is called when exception object lifetime ends
int test_dtor() {
    g_dtor_called = 0;
    try {
        throw MyException{7};
    } catch (MyException& e) {
        g_value_caught = e.value;
    }
    // __cxa_end_catch decrements the refcount and calls the dtor
    return (g_value_caught == 7 && g_dtor_called >= 1) ? 0 : 1;
}

int main() {
    if (test_catch_ref() != 42) return 1;
    if (test_catch_val() != 99) return 2;
    if (test_dtor() != 0) return 3;
    return 0;
}
