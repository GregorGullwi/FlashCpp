struct ValueBase {
	template<class Tag = void>
	operator int() const {
		return 7;
	}
};

struct PrivateUsing : private ValueBase {
private:
	using ValueBase::operator int;
};

int main() {
	PrivateUsing value;
	int converted = value;
	return converted;
}
