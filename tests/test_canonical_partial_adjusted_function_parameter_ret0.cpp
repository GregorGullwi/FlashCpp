template <class T>
struct Probe {
	static constexpr int width = 0;
};

template <class T>
struct Probe<T (**)(T(long))> {
	static constexpr int width = sizeof(T);
};

static_assert(Probe<int (**)(int (*)(long))>::width == sizeof(int));
static_assert(Probe<int (**)(int(long))>::width == sizeof(int));
static_assert(Probe<short (**)(short (*)(long))>::width == sizeof(short));
static_assert(Probe<int (**)(int (*)(int))>::width == 0);
static_assert(Probe<int (**)(short (*)(long))>::width == 0);

int main() {
	return Probe<int (**)(int (*)(long))>::width == sizeof(int) ? 0 : 1;
}
