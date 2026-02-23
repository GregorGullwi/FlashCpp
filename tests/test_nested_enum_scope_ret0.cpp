// Test that unscoped enum enumerators declared inside a struct are accessible
// within member functions of the struct (class scope resolution).
struct Widget {
    enum Op {
        Op_create = 1,
        Op_clone = 2,
        Op_destroy = 3
    };
    
    int perform(Op op) {
        if (op == Op_clone) return 42;
        if (op == Op_destroy) return 10;
        return 0;
    }
    
    int do_clone() {
        return perform(Op_clone);
    }
};

int main() {
    Widget w;
    return w.do_clone() == 42 ? 0 : 1;
}
