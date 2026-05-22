// Regression: nested out-of-line constructor templates must replay all member
// initializers (comma-separated initializer list) during instantiation.

template <typename T>
struct Outer {
struct Inner {
int left = 1;
int right = 1;
int bonus = 0;

template <typename U>
Inner(U& a, U& b);

int total() const {
return left + right + bonus;
}
};
};

template <typename T>
template <typename U>
Outer<T>::Inner::Inner(U& a, U& b)
: left(a), right(b), bonus(0) {}

int main() {
int x = 20;
int y = 22;
Outer<int>::Inner value(x, y);
return value.total();
}
