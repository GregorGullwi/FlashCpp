// Test nullptr with regular pointer in template specialization

template<typename T>
struct Container;

template<typename T>
struct Container<T*> {
    T* ptr;
    
    void reset() {
        ptr = nullptr;
    }
    
    int isNull() {
        if (ptr == nullptr) {
            return 0;
        }
        return 1;
    }
};

int main() {
    Container<int*> c;
    c.reset();
    // Should return 0 if nullptr works with regular pointers
    return c.isNull();
}
