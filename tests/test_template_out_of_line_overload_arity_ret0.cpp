template<typename T>
struct OverloadSet {
	int choose(T) const;
	int choose(T, int) const;
};

template<typename T>
int OverloadSet<T>::choose(T) const {
	return 1;
}

template<typename T>
int OverloadSet<T>::choose(T, int value) const {
	return value;
}

int main() {
	OverloadSet<int> set;
	return set.choose(7, 0);
}
