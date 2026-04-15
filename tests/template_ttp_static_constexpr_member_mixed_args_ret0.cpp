// Regression: TTP static constexpr member access must preserve both type category
// and non-type template arguments during placeholder substitution.

template<typename T, int N>
struct box2 {
static constexpr int id = sizeof(T) + N;
};

template<template<typename, int> class W>
struct probe2 {
static constexpr int sz = W<char, 5>::id;
};

int main() {
return probe2<box2>::sz - 6;
}
