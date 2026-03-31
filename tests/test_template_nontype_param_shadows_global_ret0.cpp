int N = 99;
template <int N>
int f() { return N; }
int main() { return f<0>(); }
