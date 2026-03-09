// Test exception hierarchy matching with deeper inheritance:
// catch(Base&) should catch throw Derived{} where Derived : Middle : Base

struct Base {
    virtual ~Base() {}
    int base_val = 1;
};

struct Middle : public Base {
    int mid_val = 2;
};

struct Derived : public Middle {
    int derived_val = 3;
};

int g_result = 1;

int main() {
    // Test 1: catch Base& when throwing Derived{}
    try {
        throw Derived{};
    } catch (Base& b) {
        g_result = 0;
    }
    if (g_result != 0) return 1;

    // Test 2: catch Middle& when throwing Derived{}
    g_result = 1;
    try {
        throw Derived{};
    } catch (Middle& m) {
        g_result = 0;
    }
    if (g_result != 0) return 2;

    // Test 3: catch Derived& when throwing Derived{} (exact match)
    g_result = 1;
    try {
        throw Derived{};
    } catch (Derived& d) {
        g_result = 0;
    }
    if (g_result != 0) return 3;

    return 0;
}
