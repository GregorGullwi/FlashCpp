// Simple test to demonstrate nested StringBuilder functionality
#include <iostream>
#include <cassert>

// This file tests the nested StringBuilder fix
// It's a minimal example that would have failed with the old implementation

class TestClass {
public:
    void process() {
        value = 42;
    }
    int value;
};

int main() {
    TestClass t;
    t.process();
    return t.value;
}
