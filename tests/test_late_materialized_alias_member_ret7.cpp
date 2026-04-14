// Phase 2 validation: late-materialized template with alias members
// Verifies that when a class template (Indirect<T>) is instantiated, the
// struct-local alias (HolderType) is correctly materialized so that the
// dependent qualified member access (typename HolderType::type) in a
// return type resolves properly.  This exercises the pending-sema
// normalization path after alias materialization.

template<typename T>
struct TypeHolder {
	using type = T;
	T value;
};

template<typename T>
struct Indirect {
	using HolderType = TypeHolder<T>;
	typename HolderType::type get_value() {
		return val;
	}
	T val;
};

int main() {
	Indirect<int> obj;
	obj.val = 7;
	return obj.get_value();
}
