// An out-of-line member function template definition redeclares the in-class
// declaration of the same member template. Calling the out-of-line-defined
// template from a class-template instance must resolve to the declaration, and
// overloads that differ in arity or in parameter types must select correctly
// instead of collapsing onto the first definition's identity.
template <typename OwnerTag>
struct OutOfLineIdentityBox {
	template <typename Value>
	Value identity(Value value);

	template <typename Left, typename Right>
	Right combine(Left left, Right right);

	template <typename Value>
	Value identity(Value value, int offset);

	template <typename Value>
	Value address(Value value);

	template <typename Value>
	Value* address(Value* value);
};

template <typename OwnerTag>
template <typename Value>
Value OutOfLineIdentityBox<OwnerTag>::identity(Value value) {
	return value;
}

template <typename OwnerTag>
template <typename Left, typename Right>
Right OutOfLineIdentityBox<OwnerTag>::combine(Left left, Right right) {
	return static_cast<Right>(left + right);
}

template <typename OwnerTag>
template <typename Value>
Value OutOfLineIdentityBox<OwnerTag>::identity(Value value, int offset) {
	return static_cast<Value>(value + offset);
}

template <typename OwnerTag>
template <typename Value>
Value OutOfLineIdentityBox<OwnerTag>::address(Value value) {
	return value;
}

template <typename OwnerTag>
template <typename Value>
Value* OutOfLineIdentityBox<OwnerTag>::address(Value* value) {
	return value;
}

// The same out-of-line registration paths must keep handling non-template
// member functions and static data members.
template <typename T>
struct OutOfLinePlainBox {
	int narrow(T value);
	static int storage;
};

template <typename T>
int OutOfLinePlainBox<T>::narrow(T value) {
	return static_cast<int>(value);
}

template <typename T>
int OutOfLinePlainBox<T>::storage = 7;

struct WidePayload {
	long long first;
	long long second;
};

int main() {
	OutOfLineIdentityBox<int> narrow_owner;
	if (narrow_owner.identity(42) != 42) {
		return 1;
	}
	if (narrow_owner.combine(19, 23) != 42) {
		return 2;
	}
	if (narrow_owner.identity(40, 2) != 42) {
		return 3;
	}
	if (narrow_owner.address(41) != 41) {
		return 4;
	}
	int narrow_address_target = 42;
	int* narrow_address = narrow_owner.address(&narrow_address_target);
	if (narrow_address != &narrow_address_target || *narrow_address != 42) {
		return 5;
	}

	OutOfLineIdentityBox<WidePayload> wide_owner;
	WidePayload payload{40, 2};
	if (wide_owner.identity(payload).first != 40) {
		return 6;
	}
	if (wide_owner.combine(20LL, 22LL) != 42LL) {
		return 7;
	}
	if (wide_owner.identity(41LL, 1) != 42LL) {
		return 8;
	}
	long long wide_address_target = 42;
	long long* wide_address = wide_owner.address(&wide_address_target);
	if (wide_address != &wide_address_target || *wide_address != 42) {
		return 9;
	}

	OutOfLinePlainBox<long long> plain_owner;
	if (plain_owner.narrow(42LL) != 42) {
		return 10;
	}
	if (OutOfLinePlainBox<int>::storage != 7) {
		return 11;
	}

	return 0;
}
