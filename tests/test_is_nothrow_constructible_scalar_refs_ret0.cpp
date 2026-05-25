int main() {
static_assert(__is_constructible(int, int&&));
static_assert(__is_constructible(int, const int&));
static_assert(__is_nothrow_constructible(int, int&&));
static_assert(__is_nothrow_constructible(int, const int&));
static_assert(__is_trivially_constructible(int, int&&));
static_assert(__is_trivially_constructible(unsigned long long, const unsigned int&));
return 0;
}
