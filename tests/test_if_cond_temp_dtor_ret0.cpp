// Tests that full-expression temporaries in control-flow condition expressions
// have their destructors called at the end of the condition full-expression,
// not delayed until the next expression statement (C++20 [class.temporary]/4).
int g = 0;
struct Watch {
    int v;
    Watch(int x) : v(x) { g = 1; }
    ~Watch() { g += 10; }
};

int use(const Watch& w) { return w.v; }

int main() {
    // Test 1: dtor fires after if-condition full-expression
    if (use(Watch(4)) == 4) {
        if (g != 11) return 1;
        g = 0;
    }

    // Test 2: dtor fires after while-condition full-expression
    int iters = 0;
    while (use(Watch(2)) == 2 && iters < 1) {
        iters++;
        if (g != 11) return 2;
        g = 0;
    }

    // Test 3: dtor fires after for-condition full-expression
    for (int i = 0; use(Watch(3)) == 3 && i < 1; i++) {
        if (g != 11) return 3;
        g = 0;
    }

    // Test 4: dtor fires after do-while condition
    int do_iters = 0;
    do {
        do_iters++;
    } while (use(Watch(5)) == 5 && do_iters < 1);
    if (g != 11) return 4;
    g = 0;

    // Test 5: dtor fires after switch condition
    switch (use(Watch(6))) {
    case 6:
        if (g != 11) return 5;
        break;
    default:
        return 6;
    }

    return 0;
}
