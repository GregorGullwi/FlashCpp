// Regression: nested out-of-line constructor templates must replay both base
// and member initializers during instantiation.

template <typename T>
struct Base {
int base;
Base(int b) : base(b) {}
};

template <typename T>
struct Outer {
struct Pair {
int left;
int right;

Pair() : left(1), right(1) {}
Pair(int l, int r) : left(l), right(r) {}
};

struct Inner : Base<T> {
Pair pair;
int bonus = 1;

template <typename U>
Inner(U a, U b);

int total() const {
return this->base + pair.left + pair.right + bonus;
}
};
};

template <typename T>
template <typename U>
Outer<T>::Inner::Inner(U a, U b)
: Base<T>(0), pair(static_cast<int>(a), static_cast<int>(b)), bonus(1) {}

int main() {
Outer<int>::Inner value(20, 21);
return value.total();
}
