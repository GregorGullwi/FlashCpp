namespace lib {
struct Tag {
int value;
friend constexpr int choose(Tag tag) { return tag.value; }
};
} // namespace lib

template <class T>
struct Make;

template <>
struct Make<lib::Tag> {
static constexpr lib::Tag value = {42};
};

template <class T>
struct Holder {
static constexpr int value = choose(Make<T>::value);
};

char verify_holder_value[(Holder<lib::Tag>::value == 42) ? 1 : -1];

int main() {
return sizeof(verify_holder_value) - 1;
}
