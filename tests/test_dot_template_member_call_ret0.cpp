// Test: obj.template member<T>() and ptr->template member<T>() syntax
// The 'template' keyword disambiguates dependent member template calls

struct Base {
    template<int N>
    int get() const { return N; }
};

struct Wrapper {
    Base b;
    Base* p;
    
    int test_dot() {
        return b.template get<42>();
    }
    
    int test_arrow() {
        return p->template get<42>();
    }
};

int main() {
    Wrapper w;
    Base b;
    w.b = b;
    w.p = &w.b;
    return 0;
}
