// Test out-of-line template constructor definition
template<typename T>
struct Buffer {
    Buffer(T val, int len);
    T value;
    int length;
};

template<typename T>
Buffer<T>::Buffer(T val, int len) : value(val), length(len) {
}

int main() {
    Buffer<int> b(5, 10);
    return b.value;
}
