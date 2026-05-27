// Regression: extern template declarations must suppress implicit instantiation.
// This previously attempted eager instantiation and failed in std headers like
// bits/allocator.h ("extern template class allocator<char>;").

template <typename T>
struct ShouldNotInstantiate {
	static_assert(sizeof(T) == 0, "extern template declaration instantiated template");
};

extern template class ShouldNotInstantiate<int>;

int main() {
	return 0;
}
