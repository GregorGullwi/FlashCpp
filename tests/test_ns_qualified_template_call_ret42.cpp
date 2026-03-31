// Test namespace-qualified template function calls in statement context
namespace ns {
template <typename T>
T* addressof(T& r) { return &r; }
} // namespace ns

int main() {
	int x = 42;
	int* p = ns::addressof(x);
	return *p;
}
