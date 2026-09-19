template <class T, int N>
using Bounded = T[N];
int main() { Bounded<int, 0> value; return 0; }
