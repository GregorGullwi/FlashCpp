// Test __has_builtin support for GCC/Clang atomic builtins used by libstdc++.

#if __has_builtin(__atomic_store)
int has_atomic_store = 1;
#else
int has_atomic_store = 0;
#endif

#if __has_builtin(__atomic_store_n)
int has_atomic_store_n = 1;
#else
int has_atomic_store_n = 0;
#endif

#if __has_builtin(__atomic_load)
int has_atomic_load = 1;
#else
int has_atomic_load = 0;
#endif

#if __has_builtin(__atomic_load_n)
int has_atomic_load_n = 1;
#else
int has_atomic_load_n = 0;
#endif

#if __has_builtin(__atomic_compare_exchange)
int has_atomic_compare_exchange = 1;
#else
int has_atomic_compare_exchange = 0;
#endif

#if __has_builtin(__atomic_compare_exchange_n)
int has_atomic_compare_exchange_n = 1;
#else
int has_atomic_compare_exchange_n = 0;
#endif

#if __has_builtin(__atomic_fetch_add)
int has_atomic_fetch_add = 1;
#else
int has_atomic_fetch_add = 0;
#endif

#if __has_builtin(__atomic_add_fetch)
int has_atomic_add_fetch = 1;
#else
int has_atomic_add_fetch = 0;
#endif

int main() {
	const int total =
		has_atomic_store + has_atomic_store_n +
		has_atomic_load + has_atomic_load_n +
		has_atomic_compare_exchange + has_atomic_compare_exchange_n +
		has_atomic_fetch_add + has_atomic_add_fetch;
	return total == 8 ? 2 : 0;
}
