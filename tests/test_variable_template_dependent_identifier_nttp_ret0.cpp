template<auto Index>
inline constexpr int value_slot = static_cast<int>(Index) + 1;

template<int Value>
struct ValueTag {
	static constexpr int value = Value;
};

template<typename T>
int runValueSlotCheck() {
	constexpr auto local_index = sizeof(T) - 1;
	return ValueTag<value_slot<local_index>>::value == static_cast<int>(sizeof(T)) ? 0 : 1;
}

int main() {
	return runValueSlotCheck<long long>();
}
