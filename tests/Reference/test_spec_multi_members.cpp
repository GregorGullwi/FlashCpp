// Test member functions in template specializations with multiple members

template<typename T>
struct Container;  // Primary template declared but not defined

// Specialization for pointers with multiple members
template<typename T>
struct Container<T*> {
    int count = 0;      // offset 0, 4 bytes
    T* ptr = nullptr;   // offset 8, 8 bytes (aligned to 8-byte boundary)
    
    void setCount(int c) {
        count = c;
    }
    
    void setPtr(T* p) {
        ptr = p;
    }
    
    int getCount() {
        return count;
    }
    
    T* getPtr() {
        return ptr;
    }
};

int main() {
    int x = 42;
    Container<int*> c;
    
    c.setCount(5);
    c.setPtr(&x);
    
    int resultCount = c.getCount();
    int* resultPtr = c.getPtr();
    
    // Should return 5 + 42 - 47 = 0
    return resultCount + *resultPtr - 47;
}
