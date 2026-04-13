// Regression test: non-pack template params in a variadic template are correctly
// deduced from call args when not all are provided explicitly.
//
// Bug: template<typename T, typename U, typename... Rest> func(T, U, Rest...)
// called as func<int>(1, 2, 3, 4) gave a parse/deduction error because U was
// not resolved — the direct-param pre-deduction map was skipped for templates
// with variadic parameters.
//
// Tests cover:
//  - Leading non-pack params partially explicit, rest deduced
//  - Leading non-pack params all explicit, pack deduced
//  - Non-pack params + pack, no explicit args
//  - Three non-pack params + pack, two explicit

template<typename T, typename U, typename... Rest>
int count_u_and_rest(T, U, Rest...) {
    return 10 + static_cast<int>(sizeof...(Rest));
}

template<typename T, typename U, typename V, typename... Rest>
int count_v_and_rest(T, U, V, Rest...) {
    return 100 + static_cast<int>(sizeof...(Rest));
}

template<typename T, typename U, typename... Rest>
T first_two_sum(T a, U b, Rest...) {
    return a + static_cast<T>(b);
}

int main() {
    // T=int explicit, U=int deduced, Rest={int,int} deduced → 10 + 2 = 12
    int c1 = count_u_and_rest<int>(1, 2, 3, 4);
    if (c1 != 12) return 10 + c1;

    // T=int, U=double both explicit, Rest={int,double} from call args → 10 + 2 = 12
    int c2 = count_u_and_rest<int, double>(1, 2.0, 3, 4.0);
    if (c2 != 12) return 20 + c2;

    // All deduced (no explicit args)
    int c3 = count_u_and_rest(1, 2, 3);
    if (c3 != 11) return 30 + c3;

    // Three non-pack params: T=int, U=int explicit; V=int, Rest={int} deduced
    int c4 = count_v_and_rest<int, int>(1, 2, 3, 4);
    if (c4 != 101) return 40 + c4;

    // Return value from non-pack params: a + b = 3 + 4 = 7
    int v5 = first_two_sum<int>(3, 4, 5, 6);
    if (v5 != 7) return 50;

    return 0;
}
