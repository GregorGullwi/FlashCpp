struct A
{ int a = 10; };
struct B
{ A a; B(A a_ = A()) : a(a_) {} };
int main() { B b; return b.a.a; }
