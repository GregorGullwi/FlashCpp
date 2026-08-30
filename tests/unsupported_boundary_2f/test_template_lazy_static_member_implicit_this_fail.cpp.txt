template<typename T>
struct LazyStaticMemberThis {
	int value;
	static int get() {
		return value;
	}
};

int main() {
	return LazyStaticMemberThis<int>::get();
}
