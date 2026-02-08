// Test: out-of-line template constructor with brace-init in member initializer list
// Verifies that brace-init (member{value}) doesn't confuse body_start position.

template<typename T>
struct Container {
    T data;
    int size;
    Container(T val, int sz);
};

template<typename T>
Container<T>::Container(T val, int sz) : data{val}, size{sz} {
    data = val;
    size = sz;
}

int main() {
    Container<int> c(7, 3);
    return c.data + c.size; // 7 + 3 = 10
}
