struct AggregateValue {
	int value;
};

constexpr AggregateValue bad_value{3.5};

int main() {
	return bad_value.value;
}
