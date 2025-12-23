// Test operator-> (member access) overload resolution
// This demonstrates smart pointer-like behavior

struct Data {
    int x;
    int y;
};

struct SmartPointer {
    Data* ptr;
    
    // Arrow operator - returns pointer to allow member access
    Data* operator->() {
        return ptr;
    }
};

int main() {
    Data d;
    d.x = 30;
    d.y = 70;
    
    SmartPointer sp;
    sp.ptr = &d;
    
    // Access members through operator->
    int sum = sp->x + sp->y;  // Should be 30 + 70 = 100
    
    return sum;
}
