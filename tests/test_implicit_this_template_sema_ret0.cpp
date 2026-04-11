// Test that implicit this->member resolution works in template contexts
// without falling back to parser_.get_expression_type.

template<typename T>
struct Box {
    T data;
    int tag;

    T getData() const { return data; }
    int getTag() const { return tag; }

    // implicit this->data and this->tag
    int compute() const {
        return static_cast<int>(data) + tag;
    }
};

template<typename T>
struct Wrapper {
    T inner;
    static int instance_count;

    T getInner() const { return inner; }

    int process() const {
        // implicit this->inner
        return static_cast<int>(inner) * 2;
    }
};

template<typename T>
int Wrapper<T>::instance_count = 0;

int main() {
    Box<int> b;
    b.data = 10;
    b.tag = 32;
    int a = b.compute(); // 42

    Wrapper<int> w;
    w.inner = 21;
    int c = w.process(); // 42

    Box<short> bs;
    bs.data = 5;
    bs.tag = 37;
    int d = bs.compute(); // 42

    // a(42) + c(42) + d(42) - 126 = 0
    return a + c + d - 126;
}
