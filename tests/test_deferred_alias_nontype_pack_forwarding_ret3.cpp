template<int... Values>
struct pack_size {
static constexpr int value = sizeof...(Values);
};

template<int... Values>
using forwarded_pack_size = pack_size<Values...>;

int main() {
return forwarded_pack_size<1, 2, 3>::value;
}
