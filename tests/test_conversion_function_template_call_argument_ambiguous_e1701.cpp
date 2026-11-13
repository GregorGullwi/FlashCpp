struct ValueTemplateConversion {
	template<class U>
	operator U() const {
		return U{};
	}
};

void choose(int value);
void choose(long long value);

int main() {
	choose(ValueTemplateConversion{});
	return 0;
}
