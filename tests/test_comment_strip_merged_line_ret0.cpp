// Test that // comments in merged lines don't eat subsequent code
// This reproduces the variant header issue where a multiline merge
// joins a line with a // comment and subsequent else/brace blocks

namespace ns {

template<bool, typename... _Types>
struct _Base {};

template<typename... _Types>
using _BaseAlias = _Base<true, _Types...>;

template<typename... _Types>
void visitor(auto&& fn, auto&& ref) {}

template<typename... _Types>
auto& cast_ref(auto& v) { return v; }

template<bool, typename... _Types>
struct TestStruct : _BaseAlias<_Types...>
{
    using Base = _BaseAlias<_Types...>;
    using Base::Base;

    TestStruct&
    operator=(const TestStruct& rhs)
    {
        visitor<_Types...>(
          [this](auto&& mem, auto idx) mutable
          {
            int j = idx;
            if (j == 0)
              return; // comment that would eat else branch on same line
            else
              {
                if (j == 1)
                  return;
                else
                  {
                    return;
                  }
              }
          }, cast_ref<_Types...>(rhs));
        return *this;
    }

    TestStruct(const TestStruct&) = default;
    TestStruct(TestStruct&&) = default;
    TestStruct& operator=(TestStruct&&) = default;
};

// This partial specialization MUST be outside TestStruct, not registered as member
template<typename... _Types>
struct TestStruct<true, _Types...> : _BaseAlias<_Types...>
{
    using Base = _BaseAlias<_Types...>;
    using Base::Base;
};

} // namespace ns

int main() { return 0; }
