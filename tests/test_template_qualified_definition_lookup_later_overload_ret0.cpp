namespace ns {
template <class U>
int choose(long) {
	return 42;
}

template <class T>
int call_qualified() {
	return ns::choose<T>(0);
}

template <class U>
int choose(int) {
	return 7;
}
} // namespace ns

int main() {
	return ns::call_qualified<int>() == 42 ? 0 : 1;
}
