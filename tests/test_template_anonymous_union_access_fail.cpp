// Test case: Accessing anonymous union members in templates gives "Missing identifier" error
// Status: ❌ FAILS - Compilation error (not hang)
// Bug: Anonymous union members not properly added to struct member lookup in templates
// Error: "Missing identifier: value"

template<typename T>
struct Container {
    union {
        char dummy;
        T value;
    };
    
    T& get() { 
        return value;  // This line causes "Missing identifier" error
    }
};

int main() {
    Container<int> c;
    return 0;
}
