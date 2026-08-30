template<typename T>
using broken_decltype = decltype(missing_function());

int main() {
	return 0;
}
