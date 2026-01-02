// Test explicit destructor calls for user-defined types
// This tests calling destructors manually via obj.~Type() syntax

// Counter to track destructor calls
int destructor_call_count = 0;

struct MyClass {
    int value;
    
    MyClass(int v) : value(v) {
        // Constructor
    }
    
    ~MyClass() {
        // Destructor - increment counter to prove it was called
        destructor_call_count++;
    }
};

int main() {
    destructor_call_count = 0;
    
    // Test 1: Explicit destructor call on stack object
    {
        MyClass obj(42);
        
        // Manually call destructor (like placement delete scenarios)
        obj.~MyClass();
        
        // Counter should be 1 after explicit call
        if (destructor_call_count != 1) {
            return 1;  // Failed - destructor not called
        }
    }
    // Note: When obj goes out of scope, destructor is called again (count becomes 2)
    // This is expected behavior - explicit destructor call doesn't prevent automatic one
    
    // The test passes if explicit destructor call worked (count was 1 at checkpoint)
    return 0;
}
