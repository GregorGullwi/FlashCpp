using Row = short[3];

struct Conversion {
	operator int*() const;

	template<class Tag = int>
	operator Row&() const;
};

int main() {
	Conversion value;
	return value[0];
}
