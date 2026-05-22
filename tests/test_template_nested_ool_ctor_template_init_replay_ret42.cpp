// Regression: nested out-of-line constructor templates in class templates must
// replay initializer lists during instantiation.

struct Pair {
int left;
int right;

Pair() : left(1), right(1) {}
Pair(int l, int r) : left(l), right(r) {}
};

template <typename T>
struct Outer {
struct Inner {
Pair pair;
int bonus = 1;

template <typename U>
Inner(U a, U b);

int total() const {
return pair.left + pair.right + bonus;
}
};
};

template <typename T>
template <typename U>
Outer<T>::Inner::Inner(U a, U b)
: pair(static_cast<int>(a), static_cast<int>(b)), bonus(1) {}

int main() {
Outer<int>::Inner value(20, 21);
return value.total();
}
