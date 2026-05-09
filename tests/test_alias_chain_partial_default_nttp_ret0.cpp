template<class T, T V>
struct BoolConstant {
static constexpr T value = V;
};

using FalseTag = BoolConstant<bool, false>;

template<class T>
struct EmptyProbe : BoolConstant<bool, __is_empty(T)> {
};

template<class Item>
struct AliasSelectedBranch : EmptyProbe<Item> {
};

template<bool, class If, class Else>
struct PickType {
using type = If;
};

template<class If, class Else>
struct PickType<false, If, Else> {
using type = Else;
};

template<bool Cond, class If, class Else>
using PickTypeT = typename PickType<Cond, If, Else>::type;

template<class T>
using EmptyAndNotFinal = PickTypeT<__is_final(T), FalseTag, AliasSelectedBranch<T>>;

template<int Index, class Head, bool = EmptyAndNotFinal<Head>::value>
struct HeadSlot;

template<int Index, class Head>
struct HeadSlot<Index, Head, true> {
};

template<int Index, class Head>
struct HeadSlot<Index, Head, false> {
};

template<int Index, class... Elements>
struct TupleLike;

template<int Index, class Head, class... Tail>
struct TupleLike<Index, Head, Tail...> : HeadSlot<Index, Head> {
};

struct NonEmpty {
int value;
};

int main() {
TupleLike<0, NonEmpty>* ptr = nullptr;
return ptr != nullptr;
}
