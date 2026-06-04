template<class T>
struct Sum {
	template<class U>
	int add(T left, U right);

	template<class U>
	int add(U left, U right);
};

template<class T>
template<class U>
int Sum<T>::add(T left, U right) {
	return left + right;
}

template<class T>
template<class U>
int Sum<T>::add(U left, U right) {
	return left - right;
}

int main() {
	Sum<int> sum;
	return sum.add(40, 2.0) == 42 ? 0 : 1;
}
