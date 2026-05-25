template<typename T>
struct CharTraitsLike {
using char_type = T;

static constexpr char_type to_char_type(unsigned long long value) {
return char_type(value);
}

static constexpr int length(const char_type* p) {
char_type converted = to_char_type(static_cast<unsigned long long>(*p));
return converted ? 1 : 0;
}
};

int main() {
const char chars[] = {'A', 0};
return CharTraitsLike<char>::length(chars) == 1 ? 0 : 1;
}
