extern "C" int printf(const char *, ...);

int g_ctor_count = 0;
int g_dtor_count = 0;

// Tuple-like struct that tracks object lifetime events
// to ensure CTAD works even when constructors/destructors run.
template <typename T, typename U>
struct TupleLike {
    T first;
    U second;

    TupleLike(T lhs, U rhs)
        : first(lhs), second(rhs) {
        ++g_ctor_count;
        printf("TupleLike ctor invoked (%d total)\n", g_ctor_count);
    }

    ~TupleLike() {
        ++g_dtor_count;
        printf("TupleLike dtor invoked (%d total)\n", g_dtor_count);
    }
};

// Deduction guide that should allow TupleLike instances to be
// materialized with heterogeneous arguments without spelling template args.
template <typename T, typename U>
TupleLike(T, U) -> TupleLike<T, U>;

int main() {
    {
        TupleLike pair(7, 3.5);
        if (pair.first != 7 || pair.second != 3.5) {
            return 2;
        }
    }

    // Destructor should have run for the scoped variable
    if (g_ctor_count != 1 || g_dtor_count != 1) {
        return 3;
    }

    // Exercise another deduction to ensure multiple lifetimes behave.
    {
        TupleLike tri(42, 99);
        if (tri.first != 42 || tri.second != 99) {
            return 4;
        }
    }

    return (g_ctor_count == 2 && g_dtor_count == 2) ? 0 : 5;
}
