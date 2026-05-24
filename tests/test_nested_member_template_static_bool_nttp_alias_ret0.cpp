template<typename T, T Value>
struct integral_constant {
	static constexpr T value = Value;
};

template<typename T>
struct is_location_invariant : integral_constant<bool, false> {};

struct NocopyTypes {
	int payload;
};

class FunctionBase {
public:
	static const unsigned long max_size = sizeof(NocopyTypes);
	static const unsigned long max_align = __alignof__(NocopyTypes);

	template<typename Functor>
	class BaseManager {
	protected:
		static const bool stored_locally =
			(is_location_invariant<Functor>::value &&
			 sizeof(Functor) <= max_size &&
			 __alignof__(Functor) <= max_align &&
			 (max_align % __alignof__(Functor) == 0));

		using LocalStorage = integral_constant<bool, stored_locally>;

	public:
		static int localFlag() {
			return LocalStorage::value ? 1 : 0;
		}
	};
};

int main() {
	return FunctionBase::BaseManager<int>::localFlag() == 1 ? 0 : 1;
}
