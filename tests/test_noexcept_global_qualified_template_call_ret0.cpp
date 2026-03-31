// Regression: leading-global-scope qualified template calls must parse inside noexcept(...)

namespace std_like {
template <class T>
T&& declval();

template <class T>
void fake_copy_init(T);

template <class From, class To>
constexpr bool is_nothrow_copy_init = noexcept(::std_like::fake_copy_init<To>(::std_like::declval<From>()));
} // namespace std_like

int main() {
	return std_like::is_nothrow_copy_init<int, int> ? 1 : 0;
}
