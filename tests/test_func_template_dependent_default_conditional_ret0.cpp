template<bool Condition, typename TrueType, typename FalseType>
struct Conditional {
	using type = TrueType;
};

template<typename TrueType, typename FalseType>
struct Conditional<false, TrueType, FalseType> {
	using type = FalseType;
};

template<typename Type>
struct IteratorTraits {
	using value_type = Type;
};

template<typename Type>
struct MoveIfNoexceptCond {
	static constexpr bool value = true;
};

template<typename Type>
struct Wrapper {
	Type value;

	Wrapper(Type value)
		: value(value) {
	}

	operator Type() const {
		return value;
	}
};

template<typename Iterator,
		 typename ReturnType = typename Conditional<
			 MoveIfNoexceptCond<typename IteratorTraits<Iterator>::value_type>::value,
			 Iterator,
			 Wrapper<Iterator>>::type>
ReturnType pickIteratorValue(Iterator value) {
	return ReturnType(value);
}

int main() {
	return pickIteratorValue(7) == 7 ? 0 : 1;
}
