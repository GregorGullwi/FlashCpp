namespace Lib {
    struct X {};
    int run(X) { return 0; }
}

int main() { Lib::X x; return run(x); }
