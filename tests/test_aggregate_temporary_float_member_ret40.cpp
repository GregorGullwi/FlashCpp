struct AggregatePair {
	float value;
	int marker;
};

AggregatePair make_pair() {
	return AggregatePair{40.0, 7};
}

int main() {
	AggregatePair p = make_pair();
	if (p.marker != 7)
		return 1;
	return static_cast<int>(p.value);
}
