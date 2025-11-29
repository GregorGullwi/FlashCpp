extern "C" {
    typedef int my_int_t;
}

extern "C++"
{
    template <typename _Ty>
    struct test_struct
    {
        enum : bool { value = false };
    };
}
