template <typename T>
struct Outer {
struct Inner {
int& left;
int& right;

template <typename U>
Inner(U& a, U& b);
};
};

template <typename T>
template <typename U>
Outer<T>::Inner::Inner(U& a, U& b)
: left(a), right(b) {}

int main() {
int x = 20;
int y = 22;
Outer<int>::Inner value(x, y);
return value.left + value.right;
}
