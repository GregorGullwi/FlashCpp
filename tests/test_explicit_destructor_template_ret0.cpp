// Test explicit destructor call in template context
// Pattern: obj.~T() where T is a template parameter

int destructor_count = 0;

struct Counter {
    int value;
    
    Counter(int v = 0) : value(v) {}
    
    ~Counter() {
        destructor_count++;
    }
};

// Template function that explicitly calls destructor
template<typename T>
void destroy_object(T& obj) {
    obj.~T();
}

int main() {
    destructor_count = 0;
    
    {
        Counter c(42);
        
        // Explicitly destroy via template function
        destroy_object(c);
        
        // Check destructor was called
        if (destructor_count != 1) {
            return 1;  // Failed
        }
    }
    // Automatic destructor call at end of scope (count becomes 2)
    
    return 0;
}
