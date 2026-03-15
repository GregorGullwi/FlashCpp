// Regression: _Tp(args) + binary_op at statement level must route to
// parse_expression_statement, not parse_variable_declaration.
// Previously only '.', '->' were checked after the closing paren;
// binary operators were not, so they fell through to the declaration path.
int g_constructed = 0;

struct MyVal {
    int v;
    MyVal(int x) : v(x) { g_constructed++; }
    MyVal operator+(MyVal o) const { return MyVal(v + o.v); }
};

template<typename _Tp>
struct Test {
    static void run() {
        MyVal two(2);
        // _Tp(1) + two at statement level: unambiguously an expression
        // (binary operator after ')' can never follow a declaration)
        _Tp(1) + two;
    }
};

int main() {
    Test<MyVal>::run();
    // two(2): g_constructed=1
    // _Tp(1): g_constructed=2
    // operator+(result): g_constructed=3
    return g_constructed == 3 ? 0 : 1;
}
