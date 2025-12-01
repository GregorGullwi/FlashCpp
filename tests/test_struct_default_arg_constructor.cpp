struct A
{
    int a = 10;
    int a2 = 1;
    A() = default;
    A(int a_) : a(a_) {}
    A(int a_, int a2_) : a(a_), a2(a2_) {}
};

struct B
{
    A a;
    int b2;
    B(int b2_, A a_ = A()) : a(a_.a, 3), b2(b2_) {}
}
;
int main()
{
    B b(5);
    B b2(4, A(2));
    return b.a.a + b2.a.a + b.b2 + b2.b2;
}
