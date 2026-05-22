// Regression test: out-of-line constructor with an initializer list for a
// class template replays the initializer list correctly at instantiation time.

template <typename T>
struct Pair {
T first;
T second;
int tag;

Pair(T a, T b, int t);
};

template <typename T>
Pair<T>::Pair(T a, T b, int t) : first(a), second(b), tag(t) {}

int main() {
Pair<int> p(10, 20, 3);
int sum = p.first + p.second + p.tag;
// 10 + 20 + 3 == 33
return sum - 33;  // return 0 on success
}
