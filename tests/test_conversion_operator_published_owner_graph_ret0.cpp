template<class T> struct Base {
	operator T() const { return T(41); }
};
template<class T> struct Primary : Base<T> {};
template<class T> struct Owner;
template<class T> struct Owner<T*> : Primary<T> {};
template<> struct Owner<long> : Base<short> {};
struct Virtual : virtual Base<int> {};
struct Hidden : Base<long long> {
	operator long long() const { return 42; }
};
struct Cv : Base<int> {
	operator int() { return 43; }
	operator int() const { return 44; }
};
struct Explicit : Base<short> {
	explicit operator int() const { return 45; }
};

long long read(long long value) { return value; }

int main() {
	Owner<long long*> partial{};
	Owner<long> full{};
	Virtual virtual_owner{};
	Hidden hidden{};
	Cv mutable_owner{};
	const Cv const_owner{};
	Explicit explicit_owner{};
	long long inherited = partial;
	short specialized = full;
	int virtual_value = virtual_owner;
	long long hidden_value = hidden;
	int mutable_value = mutable_owner;
	int const_value = const_owner;
	int implicit_value = explicit_owner;
	int explicit_value = static_cast<int>(explicit_owner);
	return inherited == 41 && specialized == 41 && virtual_value == 41 && hidden_value == 42 && mutable_value == 43 &&
		const_value == 44 && implicit_value == 41 && explicit_value == 45 && read(partial) == 41 ? 0 : 1;
}
