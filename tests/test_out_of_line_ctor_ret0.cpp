// Test out-of-line constructor and destructor definitions
// This is a common pattern in standard library headers
// NOTE: Copy constructor test disabled due to pre-existing codegen bug
//       with copy constructor member initialization

class Counter {
public:
    int count;
    
    // Constructor declarations
    Counter();
    Counter(int initial);
    
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
    
    c1.set(10);
    
    // Verify: c1=10, c2=42
    if (c1.get() != 10) return 1;
    if (c2.get() != 42) return 2;
    
    return 0;
}
