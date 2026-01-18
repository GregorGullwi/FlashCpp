// Test out-of-line constructor and destructor definitions
// This is a common pattern in standard library headers

class Counter {
public:
    int count;
    
    // Constructor declarations
    Counter();
    Counter(int initial);
    Counter(const Counter& other);
    
    // Destructor declaration
    ~Counter();
    
    // Method declarations
    int get() const;
    void set(int val);
};

// Out-of-line default constructor
Counter::Counter() : count(0) {
}

// Out-of-line parameterized constructor
Counter::Counter(int initial) : count(initial) {
}

// Out-of-line copy constructor
Counter::Counter(const Counter& other) : count(other.count) {
}

// Out-of-line destructor
Counter::~Counter() {
}

// Out-of-line const member function
int Counter::get() const {
    return count;
}

// Out-of-line non-const member function
void Counter::set(int val) {
    count = val;
}

int main() {
    Counter c1;         // default ctor
    Counter c2(42);     // parameterized ctor
    Counter c3(c2);     // copy ctor
    
    c1.set(10);
    
    // Verify: c1=10, c2=42, c3=42 (copy of c2)
    if (c1.get() != 10) return 1;
    if (c2.get() != 42) return 2;
    if (c3.get() != 42) return 3;
    
    return 0;
}
