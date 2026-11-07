// Keep the named-concept chain exercised under the normal 1 MiB compiler stack.
struct NotIntegral {};

template <typename T>
concept IntegralChain0 = __is_integral(T);

template <typename T>
concept IntegralChain1 = IntegralChain0<T> && IntegralChain0<T>;

template <typename T>
concept IntegralChain2 = IntegralChain1<T> && IntegralChain0<T>;

template <typename T>
concept IntegralChain3 = IntegralChain2<T> && IntegralChain0<T>;

template <typename T>
concept IntegralChain4 = IntegralChain3<T> && IntegralChain0<T>;

template <typename T>
concept IntegralChain5 = IntegralChain4<T> && IntegralChain0<T>;

template <typename T>
concept IntegralChain6 = IntegralChain5<T> && IntegralChain0<T>;

template <typename T>
concept IntegralChain7 = IntegralChain6<T> && IntegralChain0<T>;

int main() {
	static_assert(IntegralChain7<char>);
	static_assert(IntegralChain7<short>);
	static_assert(IntegralChain7<long long>);
	static_assert(!IntegralChain7<NotIntegral>);
	return IntegralChain7<int> ? 0 : 1;
}
