struct A
{
    int a = 10;
    A() = default;
    A(int a_) : a(a_) {}
};

struct B
{
    A a;
    B(A a_ = A()) : a(a_) {}
}
;
int main()
{
    B b;
    B b2(A(2));
    return b.a.a + b2.a.a;
}
