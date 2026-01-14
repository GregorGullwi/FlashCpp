// Test that deleted special member functions are tracked correctly
// Verifies that the parser tracks deleted constructors, destructors, and assignment operators

struct NonCopyable {
    int x;
    
    // Default constructor is allowed
    NonCopyable() = default;
    
    // Delete copy constructor
    NonCopyable(const NonCopyable& other) = delete;
    
    // Delete copy assignment operator  
    NonCopyable& operator=(const NonCopyable& other) = delete;
};

struct NonMovable {
    int y;
    
    // Default constructor
    NonMovable() = default;
    
    // Copy is allowed
    NonMovable(const NonMovable& other) = default;
    NonMovable& operator=(const NonMovable& other) = default;
    
    // Delete move constructor and assignment
    NonMovable(NonMovable&& other) = delete;
    NonMovable& operator=(NonMovable&& other) = delete;
};

struct NoDefaultCtor {
    int z;
    
    // Delete default constructor
    NoDefaultCtor() = delete;
    
    // Provide a custom constructor
    NoDefaultCtor(int val) : z(val) {}
};

struct DeletedDestructor {
    int w;
    
    DeletedDestructor() = default;
    
    // Delete destructor (unusual but valid)
    ~DeletedDestructor() = delete;
};

int main() {
    NonCopyable nc;
    nc.x = 10;
    
    NonMovable nm;
    nm.y = 20;
    
    NoDefaultCtor nd(42);
    
    // Cannot create DeletedDestructor on stack since it can't be destroyed
    // DeletedDestructor* dd = new DeletedDestructor(); // Would leak memory
    
    return nc.x + nm.y + nd.z - 72;  // Returns 0
}
