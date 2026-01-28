// Test regular pointer nullptr in member

template<typename T>
struct Container;

template<typename T>
struct Container<T*> {
    int* ptr;
    
    int testNull() {
        ptr = nullptr;
        if (ptr) {
            return 1;  // non-null
        }
        return 0;  // null
    }
};

int main() {
    Container<int*> c;
    return c.testNull();
}
