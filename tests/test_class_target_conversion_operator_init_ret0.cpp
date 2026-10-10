struct ClassTarget {
	int value;
	int next;
	int last;
	ClassTarget(int input) : value(input), next(input + 1), last(input + 2) {}
	ClassTarget(const ClassTarget&) = delete;
	ClassTarget(ClassTarget&&) = delete;
};

struct ConversionSource {
	int value;
	operator ClassTarget() const { return ClassTarget{value}; }
};

struct AggregateTarget { int value; };

struct AggregateSource {
	int value;
	operator AggregateTarget() const { return AggregateTarget{value}; }
};

int main() {
	ClassTarget copied = ConversionSource{7};
	ClassTarget direct(ConversionSource{9});
	AggregateTarget aggregate_copied = AggregateSource{11};
	AggregateTarget aggregate_direct(AggregateSource{13});
	return copied.value == 7 && copied.next == 8 && copied.last == 9 && direct.value == 9 && direct.next == 10 &&
		direct.last == 11 && aggregate_copied.value == 11 && aggregate_direct.value == 13 ? 0 : 1;
}
