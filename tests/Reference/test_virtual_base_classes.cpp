// Test virtual base classes (virtual inheritance)
// Virtual inheritance solves the diamond problem by ensuring only one copy of the base class

struct Base {
    int value;
    Base(int v) : value(v) {}
};

// Left inherits virtually from Base
struct Left : virtual public Base {
    int left_data;
    Left(int v, int l) : Base(v), left_data(l) {}
};

// Right inherits virtually from Base
struct Right : virtual public Base {
    int right_data;
    Right(int v, int r) : Base(v), right_data(r) {}
};

// Diamond inherits from both Left and Right
// With virtual inheritance, there's only ONE copy of Base
struct Diamond : public Left, public Right {
    int diamond_data;
    // Note: With virtual inheritance, the most derived class (Diamond) must initialize the virtual base
    Diamond(int v, int l, int r, int d) 
        : Base(v), Left(v, l), Right(v, r), diamond_data(d) {}
};

int main() {
    Diamond d(100, 10, 20, 30);
    
    // Access the single shared Base::value
    int base_value = d.value;  // 100 (only one copy)
    
    // Access members from Left path
    int left_value = d.left_data;  // 10
    
    // Access members from Right path
    int right_value = d.right_data;  // 20
    
    // Access diamond's own member
    int diamond_value = d.diamond_data;  // 30
    
    return base_value + left_value + right_value + diamond_value;  // Expected: 160
}

