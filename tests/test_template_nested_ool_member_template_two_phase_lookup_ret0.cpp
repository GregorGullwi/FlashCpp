// Regression test: out-of-line nested member function template (template<T>
// template<U>) preserves the definition-time lookup context when its body is
// replayed at instantiation time.
// The helper() free function is called inside convert(); two-phase lookup
// must resolve it at definition time.

namespace A {
int helper(int v) { return v * 2; }

template <typename T>
struct Box {
T value;
Box(T v) : value(v) {}

// Member function template — inner template param is U, returns int.
template <typename U>
int scale(U factor);
};

// Out-of-line definition: template<T> template<U>
template <typename T>
template <typename U>
int Box<T>::scale(U factor) {
return helper(static_cast<int>(factor));
}
}

int main() {
A::Box<int> b(0);
int result = b.scale(21);
// helper(21) == 42
return result - 42;
}
