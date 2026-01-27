template<int N>
struct Inner {
    int data[N];
};

template<int M>
struct Outer {
    Inner<M> inner;
};

int main() {
    Outer<5> outer;
    return 0;
}