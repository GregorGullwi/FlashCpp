struct PromotionTemplateConversion {
	operator short() const {
		return 1;
	}

	template<class U>
	operator U() const {
		return U{2};
	}
};

void choose(int value);
void choose(long long value);

int main() {
	choose(PromotionTemplateConversion{});
	return 0;
}
