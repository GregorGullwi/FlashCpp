namespace ns {
	template<bool Flag = false, typename T>
	int f(T value) {
		return Flag ? value : 0;
	}
}

template<bool Flag>
int g() {
	return ns::f<Flag>(1);
}

int main() {
	return g<true>();
}
