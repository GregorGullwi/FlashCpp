template<long long Value>
struct Number {
	static constexpr long long value = Value;
};

template<long long Value>
struct Sign {
	static constexpr int value = (Value > 0) - (Value < 0);
};

template<typename Left, typename Right,
		 bool DifferentSigns = (Left::value == 0 || Right::value == 0
								|| Sign<Left::value>::value != Sign<Right::value>::value),
		 bool BothNegative = (Sign<Left::value>::value == -1
							  && Sign<Right::value>::value == -1)>
struct CompareFlags {
	static constexpr bool differentSigns = DifferentSigns;
	static constexpr bool bothNegative = BothNegative;
};

int main() {
	static_assert(CompareFlags<Number<2>, Number<3>>::differentSigns == false);
	static_assert(CompareFlags<Number<2>, Number<3>>::bothNegative == false);
	static_assert(CompareFlags<Number<-2>, Number<-3>>::differentSigns == false);
	static_assert(CompareFlags<Number<-2>, Number<-3>>::bothNegative == true);
	static_assert(CompareFlags<Number<-2>, Number<3>>::differentSigns == true);
	static_assert(CompareFlags<Number<-2>, Number<3>>::bothNegative == false);
	return CompareFlags<Number<-2>, Number<-3>>::bothNegative ? 1 : 0;
}
