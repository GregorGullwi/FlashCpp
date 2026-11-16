template<class T>
struct ConditionalClassTemplateConversion {
	explicit(sizeof(T) > sizeof(int)) operator int() const {
		return 2;
	}
};

int main() {
	ConditionalClassTemplateConversion<long long> value;
	int converted = value;
	return converted;
}
