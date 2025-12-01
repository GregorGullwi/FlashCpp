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
};

// Template version to test template path with primitive default arguments
template<typename T>
struct TC
{
    T val;
    T val2;
    TC() : val(10), val2(1) {}
    TC(T v) : val(v), val2(1) {}
    TC(T v, T v2) : val(v), val2(v2) {}
};

template<typename T>
struct TD
{
    TC<T> c;
    T d;
    // Use primitive default argument instead of template type default
    TD(T d_, T c_val = 10) : c(c_val, 3), d(d_) {}
};

int main()
{
    B b(5);
    B b2(4, A(2));
    
    // Test template versions with default argument
    TD<int> td(5);       // Uses default c_val = 10
    TD<int> td2(4, 2);   // Uses explicit c_val = 2
    
    return b.a.a + b2.a.a + b.b2 + b2.b2 + td.c.val + td2.c.val + td.d + td2.d;
}
