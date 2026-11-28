using Parameter = int(long);

struct Owner {
	int method();
};
Owner owner;
using GroupedMemberParameter = int (*)(int (decltype(owner)::*)());

template <class T>
struct Probe {
	static constexpr int width = 0;
};

template <class T>
struct Probe<T (**)(Parameter)> {
	static constexpr int width = sizeof(T);
};

static_assert(Probe<int (**)(int (*)(long))>::width == sizeof(int));
static_assert(Probe<int (**)(Parameter)>::width == sizeof(int));
static_assert(Probe<short (**)(int (*)(long))>::width == sizeof(short));
static_assert(Probe<int (**)(int (*)(int))>::width == 0);

int main() {
	return Probe<int (**)(int (*)(long))>::width == sizeof(int) ? 0 : 1;
}
