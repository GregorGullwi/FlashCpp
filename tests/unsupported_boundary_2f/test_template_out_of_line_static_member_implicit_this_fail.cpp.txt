template<typename T>
struct OutOfLineStaticMemberThis {
	int value;
	static int get();
};

template<typename T>
int OutOfLineStaticMemberThis<T>::get() {
	return value;
}

int main() {
	return OutOfLineStaticMemberThis<int>::get();
}
