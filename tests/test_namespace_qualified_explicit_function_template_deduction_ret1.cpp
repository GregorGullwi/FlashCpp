namespace ns {
	template<typename Tag, typename T>
	int f(T value) {
		return value;
	}
}

template<typename Tag>
int g() {
	return ns::f<Tag>(1);
}

int main() {
	return g<void>();
}
