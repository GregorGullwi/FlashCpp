namespace adl_decltype_hidden_friend {
	struct Tag {
		friend int hidden(Tag) {
			return 17;
		}
	};
}

int main() {
	using ReturnType = decltype(hidden(adl_decltype_hidden_friend::Tag{}));
	ReturnType value = 17;
	return value - 17;
}
