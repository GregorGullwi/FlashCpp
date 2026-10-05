template<int N> struct Deep;
template<int N> using BaseAlias = Deep<N>;
template<int N> struct Deep : BaseAlias<N - 1> {};
template<> struct Deep<0> { char value; };

static_assert(sizeof(Deep<1024>) == sizeof(char));

int main() {
	return 0;
}
