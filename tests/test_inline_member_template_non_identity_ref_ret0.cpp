int member_global = 17;

template <class Owner>
struct Wrapper {
	template <class T>
	inline T& return_global(T& ignored) {
		return member_global;
	}
};

int main() {
	Wrapper<long> wrapper;
	int local = 5;
	wrapper.return_global<int>(local) = 23;
	return member_global == 23 && local == 5 ? 0 : 1;
}
