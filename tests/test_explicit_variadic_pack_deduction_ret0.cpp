// Regression test for Phase 6: pack-aware explicit template argument deduction.
// When a variadic template function is called with explicit template args for some
// parameters, the remaining variadic pack elements must be deduced from the
// corresponding call argument types (not left as an empty pack).
//
// Bug: count_rest<int>(1, 2, 3) was deducing Rest as {} instead of {int, int}.
//
// Tests cover:
//  - Simple: T explicit, Rest... deduced from call args
//  - Multi: T + U explicit, Rest... deduced
//  - Mixed pack: one explicit pack element, remainder deduced from call args
//  - Return: first_of<T>(a, ...) returns T value correctly
//  - Empty pack: no extra call args → empty pack (valid)
//  - All-explicit: all pack args provided in <...>

template<typename T, typename... Rest>
int count_rest(T, Rest...) { return static_cast<int>(sizeof...(Rest)); }

template<typename T, typename U, typename... Rest>
int count_after_two(T, U, Rest...) { return static_cast<int>(sizeof...(Rest)); }

template<typename First, typename... Others>
First first_of(First a, Others...) { return a; }

template<typename T, typename... Empty>
int count_empty(T) { return static_cast<int>(sizeof...(Empty)); }

template<typename... Args>
int count_all_explicit(Args...) { return static_cast<int>(sizeof...(Args)); }

template<typename... Args>
int count_mixed_pack(Args...) { return static_cast<int>(sizeof...(Args)); }

int main() {
    // T=int explicit; Rest={int,int} deduced from second and third call args
    int c1 = count_rest<int>(1, 2, 3);
    if (c1 != 2) return 10 + c1;

    // T=int, U=double explicit; Rest={int,double} deduced from remaining call args
    int c2 = count_after_two<int, double>(1, 2.0, 3, 4.0);
    if (c2 != 2) return 20 + c2;

    // One explicit pack element, remainder deduced from the call-arg slice.
    int c3 = count_mixed_pack<int>(1, 2.0);
    if (c3 != 2) return 30 + c3;

    // First=int explicit; function returns first arg (42)
    int v4 = first_of<int>(42, 1, 2);
    if (v4 != 42) return 40;

    // T explicit, no extra call args → empty pack (sizeof...(Empty) == 0)
    int c5 = count_empty<int>(99);
    if (c5 != 0) return 50 + c5;

    // All pack args explicit
    int c6 = count_all_explicit<int, double, float>(1, 2.0, 3.0f);
    if (c6 != 3) return 60 + c6;

    return 0;
}
