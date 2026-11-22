// A skipped operand of && or || must not be substituted or instantiated.
template<class T> struct Explode { static_assert(sizeof(T) == 0); };

template<class T> struct PickAnd { static constexpr int value = 0; };
template<class T> requires (sizeof(T) == 1 && sizeof(Explode<T>) != 0) struct PickAnd<T*> { static constexpr int value = 1; };

template<class T> concept ShortCircuited = sizeof(T) == 1 || sizeof(Explode<T>) != 0;
template<class T> struct PickOr { static constexpr int value = 0; };
template<class T> requires ShortCircuited<T> struct PickOr<T*> { static constexpr int value = 2; };

int main() {
	return PickAnd<int*>::value + (PickOr<char*>::value != 2);
}
