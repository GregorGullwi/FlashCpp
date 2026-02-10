// Test: pseudo-destructor call with template arguments
// ptr->~Type<Args>() must parse correctly

template<typename T>
struct Node {
    T value;
};

void destroy(Node<int>* p) {
    p->~Node<int>();
}

int main() {
    return 0;
}
