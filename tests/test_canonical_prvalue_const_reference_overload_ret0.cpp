struct Record {
	long long value;

	operator int() const {
		return static_cast<int>(value);
	}
};

int bindInt(const int& value) {
	return value;
}

int bindDouble(const double& value) {
	return static_cast<int>(value);
}

int bindConvertedDouble(const double& value) {
	return static_cast<int>(value);
}

int bindRecord(const Record& value) {
	return static_cast<int>(value.value);
}

int selectReference(const int&) {
	return 1;
}

int selectReference(int&&) {
	return 2;
}

int selectConversionRank(const double&) {
	return 1;
}

int selectConversionRank(int) {
	return 2;
}

template<typename T>
int bindTemplate(const T& value) {
	return static_cast<int>(value);
}

int main() {
	if (bindInt(41) != 41) {
		return 1;
	}
	if (bindDouble(6.75) != 6) {
		return 2;
	}
	if (bindRecord(Record{27}) != 27) {
		return 3;
	}
	if (bindTemplate(33LL) != 33) {
		return 4;
	}
	if (bindTemplate(Record{34}) != 34) {
		return 5;
	}
	if (bindConvertedDouble(8) != 8) {
		return 6;
	}
	short promoted_value = 9;
	if (selectConversionRank(promoted_value) != 2) {
		return 7;
	}
	return selectReference(35) == 2 ? 0 : 8;
}
