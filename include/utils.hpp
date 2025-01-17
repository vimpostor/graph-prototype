#ifndef GNURADIO_GRAPH_UTILS_HPP
#define GNURADIO_GRAPH_UTILS_HPP

#include <functional>
#include <iostream>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>

#include "typelist.hpp"
#include "vir/simd.h"

#ifndef __EMSCRIPTEN__
#include <cxxabi.h>
#include <iostream>
#include <typeinfo>
#endif

#ifndef DISABLE_SIMD
#define DISABLE_SIMD 0
#endif

namespace fair::literals {
// C++23 has literal suffixes for std::size_t, but we are not
// in C++23 just yet
constexpr std::size_t operator"" _UZ(unsigned long long n) { return static_cast<std::size_t>(n); }
} // namespace fair::literals

namespace fair::meta {

using namespace fair::literals;

template<typename... Ts>
struct print_types;

template<typename CharT, std::size_t SIZE>
struct fixed_string {
    constexpr static std::size_t N            = SIZE;
    CharT                        _data[N + 1] = {};

    constexpr fixed_string()                  = default;

    constexpr explicit(false) fixed_string(const CharT (&str)[N + 1]) noexcept {
        if constexpr (N != 0)
            for (std::size_t i = 0; i < N; ++i) _data[i] = str[i];
    }

    [[nodiscard]] constexpr std::size_t
    size() const noexcept {
        return N;
    }

    [[nodiscard]] constexpr bool
    empty() const noexcept {
        return N == 0;
    }

    [[nodiscard]] constexpr explicit operator std::string_view() const noexcept { return { _data, N }; }

    [[nodiscard]] explicit           operator std::string() const noexcept { return { _data, N }; }

    [[nodiscard]]                    operator const char *() const noexcept { return _data; }

    [[nodiscard]] constexpr bool
    operator==(const fixed_string &other) const noexcept {
        return std::string_view{ _data, N } == std::string_view(other);
    }

    template<std::size_t N2>
    [[nodiscard]] friend constexpr bool
    operator==(const fixed_string &, const fixed_string<CharT, N2> &) {
        return false;
    }
};

template<typename CharT, std::size_t N>
fixed_string(const CharT (&str)[N]) -> fixed_string<CharT, N - 1>;

template<typename T>
struct is_fixed_string : std::false_type {};

template<typename CharT, std::size_t N>
struct is_fixed_string<fair::meta::fixed_string<CharT, N>> : std::true_type {};

template<typename T>
concept FixedString = is_fixed_string<T>::value;

template<typename CharT, std::size_t N1, std::size_t N2>
constexpr fixed_string<CharT, N1 + N2>
operator+(const fixed_string<CharT, N1> &lhs, const fixed_string<CharT, N2> &rhs) noexcept {
    meta::fixed_string<CharT, N1 + N2> result{};
    for (std::size_t i = 0; i < N1; ++i) {
        result._data[i] = lhs._data[i];
    }
    for (std::size_t i = 0; i < N2; ++i) {
        result._data[N1 + i] = rhs._data[i];
    }
    result._data[N1 + N2] = '\0';
    return result;
}

template<typename T>
[[nodiscard]] std::string
type_name() noexcept {
#if !defined(__EMSCRIPTEN__)
    std::string type_name = typeid(T).name();
    int         status;
    char       *demangled_name = abi::__cxa_demangle(type_name.c_str(), nullptr, nullptr, &status);
    if (status == 0) {
        std::string ret(demangled_name);
        free(demangled_name);
        return ret;
    } else {
        free(demangled_name);
        return typeid(T).name();
    }
#else
    return typeid(T).name(); // TODO: to be replaced by refl-cpp
#endif
}

template<fixed_string val>
struct message_type {};

template<class... T>
constexpr bool        always_false  = false;

constexpr std::size_t invalid_index = -1_UZ;

#if HAVE_SOURCE_LOCATION
[[gnu::always_inline]] inline void
precondition(bool cond, const std::source_location loc = std::source_location::current()) {
    struct handle {
        [[noreturn]] static void
        failure(std::source_location const &loc) {
            std::clog << "failed precondition in " << loc.file_name() << ':' << loc.line() << ':' << loc.column() << ": `" << loc.function_name() << "`\n";
            __builtin_trap();
        }
    };

    if (not cond) [[unlikely]]
        handle::failure(loc);
}
#else
[[gnu::always_inline]] inline void
precondition(bool cond) {
    struct handle {
        [[noreturn]] static void
        failure() {
            std::clog << "failed precondition\n";
            __builtin_trap();
        }
    };

    if (not cond) [[unlikely]]
        handle::failure();
}
#endif

/**
 * T is tuple-like if it implements std::tuple_size, std::tuple_element, and std::get.
 * Tuples with size 0 are excluded.
 */
template<typename T>
concept tuple_like = (std::tuple_size<T>::value > 0) && requires(T tup) {
                                                            { std::get<0>(tup) } -> std::same_as<typename std::tuple_element_t<0, T> &>;
                                                        };

static_assert(!tuple_like<int>);
static_assert(!tuple_like<std::tuple<>>);
static_assert(tuple_like<std::tuple<int>>);
static_assert(tuple_like<std::tuple<int &>>);
static_assert(tuple_like<std::tuple<const int &>>);
static_assert(tuple_like<std::tuple<const int>>);
static_assert(!tuple_like<std::array<int, 0>>);
static_assert(tuple_like<std::array<int, 2>>);
static_assert(tuple_like<std::pair<int, short>>);

template<template<typename...> class Template, typename Class>
struct is_instantiation : std::false_type {};

template<template<typename...> class Template, typename... Args>
struct is_instantiation<Template, Template<Args...>> : std::true_type {};
template<typename Class, template<typename...> class Template>
concept is_instantiation_of = is_instantiation<Template, Class>::value;

template<typename T>
concept map_type = is_instantiation_of<T, std::map> || is_instantiation_of<T, std::unordered_map>;

template<typename T>
concept vector_type = is_instantiation_of<T, std::vector>;

namespace stdx      = vir::stdx;

template<typename V, typename T = void>
concept any_simd = stdx::is_simd_v<V> && (std::same_as<T, void> || std::same_as<T, typename V::value_type>);

template<typename V, typename T>
concept t_or_simd = std::same_as<V, T> || any_simd<V, T>;

template<typename T>
concept vectorizable_v = std::constructible_from<stdx::simd<T>>;

template<typename T>
using vectorizable = std::integral_constant<bool, vectorizable_v<T>>;

/**
 * Determines the SIMD width of the given structure. This can either be a stdx::simd object or a
 * tuple-like of stdx::simd (recursively). The latter requires that the SIMD width is homogeneous.
 * If T is not a simd (or tuple thereof), value is 0.
 */
template<typename T>
struct simdize_size : std::integral_constant<std::size_t, 0> {};

template<typename T, typename A>
struct simdize_size<stdx::simd<T, A>> : stdx::simd_size<T, A> {};

template<tuple_like Tup>
struct simdize_size<Tup> : simdize_size<std::tuple_element_t<0, Tup>> {
    static_assert([]<std::size_t... Is>(std::index_sequence<Is...>) {
        return ((simdize_size<std::tuple_element_t<0, Tup>>::value == simdize_size<std::tuple_element_t<Is, Tup>>::value) && ...);
    }(std::make_index_sequence<std::tuple_size_v<Tup>>()));
};

template<typename T>
inline constexpr std::size_t simdize_size_v = simdize_size<T>::value;

namespace detail {
/**
 * Shortcut to determine the stdx::simd specialization with the most efficient ABI tag for the
 * requested element type T and width N.
 */
template<typename T, std::size_t N>
using deduced_simd = stdx::simd<T, stdx::simd_abi::deduce_t<T, N>>;

template<typename T, std::size_t N>
struct simdize_impl;

template<vectorizable_v T, std::size_t N>
    requires requires { typename stdx::native_simd<T>; }
struct simdize_impl<T, N> {
    using type = deduced_simd<T, N == 0 ? stdx::native_simd<T>::size() : N>;
};

template<std::size_t N>
struct simdize_impl<std::tuple<>, N> {
    using type = std::tuple<>;
};

template<tuple_like Tup, std::size_t N>
    requires requires { typename simdize_impl<std::tuple_element_t<0, Tup>, N>::type; }
struct simdize_impl<Tup, N> {
    static inline constexpr std::size_t size = N > 0 ? N : []<std::size_t... Is>(std::index_sequence<Is...>) constexpr {
        return std::max({ simdize_size_v<typename simdize_impl<std::tuple_element_t<Is, Tup>, 0>::type>... });
    }(std::make_index_sequence<std::tuple_size_v<Tup>>());

    using type                               = decltype([]<std::size_t... Is>(std::index_sequence<Is...>) -> std::tuple<typename simdize_impl<std::tuple_element_t<Is, Tup>, size>::type...> {
        return {};
    }(std::make_index_sequence<std::tuple_size_v<Tup>>()));
};
} // namespace detail

/**
 * Meta-function that turns a vectorizable type or a tuple-like (recursively) of vectorizable types
 * into a stdx::simd or std::tuple (recursively) of stdx::simd. If N is non-zero, N determines the
 * resulting SIMD width. Otherwise, of all vectorizable types U the maximum
 * stdx::native_simd<U>::size() determines the resulting SIMD width.
 */
template<typename T, std::size_t N = 0>
using simdize = typename detail::simdize_impl<T, N>::type;

static_assert(std::same_as<simdize<std::tuple<std::tuple<int, double>, short, std::tuple<float>>>,
                           std::tuple<std::tuple<detail::deduced_simd<int, stdx::native_simd<short>::size()>, detail::deduced_simd<double, stdx::native_simd<short>::size()>>, stdx::native_simd<short>,
                                      std::tuple<detail::deduced_simd<float, stdx::native_simd<short>::size()>>>>);

template<fixed_string Name, typename PortList>
consteval int
indexForName() {
    auto helper = []<std::size_t... Ids>(std::index_sequence<Ids...>) {
        constexpr int n_matches = ((PortList::template at<Ids>::static_name() == Name) + ...);
        static_assert(n_matches <= 1, "Multiple ports with that name were found. The name must be unique. You can "
                                      "still use a port index instead.");
        static_assert(n_matches == 1, "No port with the given name exists.");
        int result = -1;
        ((PortList::template at<Ids>::static_name() == Name ? (result = Ids) : 0), ...);
        return result;
    };
    return helper(std::make_index_sequence<PortList::size>());
}

// template<template<typename...> typename Type, typename... Items>
// using find_type = decltype(std::tuple_cat(std::declval<std::conditional_t<is_instantiation_of<Items, Type>, std::tuple<Items>, std::tuple<>>>()...));

template<template<typename> typename Pred, typename... Items>
struct find_type;

template<template<typename> typename Pred>
struct find_type<Pred> {
    using type = std::tuple<>;
};

template<template<typename> typename Pred, typename First, typename... Rest>
struct find_type<Pred, First, Rest...> {
    using type = decltype(std::tuple_cat(std::conditional_t<Pred<First>::value, std::tuple<First>, std::tuple<>>(), typename find_type<Pred, Rest...>::type()));
};

template<template<typename> typename Pred, typename... Items>
using find_type_t = typename find_type<Pred, Items...>::type;

template<typename Tuple, typename Default = void>
struct get_first_or_default;

template<typename First, typename... Rest, typename Default>
struct get_first_or_default<std::tuple<First, Rest...>, Default> {
    using type = First;
};

template<typename Default>
struct get_first_or_default<std::tuple<>, Default> {
    using type = Default;
};

template<typename Tuple, typename Default = void>
using get_first_or_default_t = typename get_first_or_default<Tuple, Default>::type;

template<typename... Lambdas>
struct overloaded : Lambdas... {
    using Lambdas::operator()...;
};

template<typename... Lambdas>
overloaded(Lambdas...) -> overloaded<Lambdas...>;

namespace detail {
template<template<typename...> typename Mapper, template<typename...> typename Wrapper, typename... Args>
Wrapper<Mapper<Args>...> *
type_transform_impl(Wrapper<Args...> *);

template<template<typename...> typename Mapper, typename T>
Mapper<T> *
type_transform_impl(T *);

template<template<typename...> typename Mapper>
void *
type_transform_impl(void *);
} // namespace detail

template<template<typename...> typename Mapper, typename T>
using type_transform = std::remove_pointer_t<decltype(detail::type_transform_impl<Mapper>(static_cast<T *>(nullptr)))>;

template<typename Arg, typename... Args>
auto
safe_min(Arg &&arg, Args &&...args) {
    if constexpr (sizeof...(Args) == 0) {
        return arg;
    } else {
        return std::min(std::forward<Arg>(arg), std::forward<Args>(args)...);
    }
}

template<typename Function, typename Tuple, typename... Tuples>
auto
tuple_for_each(Function &&function, Tuple &&tuple, Tuples &&...tuples) {
    static_assert(((std::tuple_size_v<std::remove_cvref_t<Tuple>> == std::tuple_size_v<std::remove_cvref_t<Tuples>>) &&...));
    return [&]<std::size_t... Idx>(std::index_sequence<Idx...>) {
        (([&function, &tuple, &tuples...](auto I) { function(std::get<I>(tuple), std::get<I>(tuples)...); }(std::integral_constant<std::size_t, Idx>{}), ...));
    }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tuple>>>());
}

template<typename Function, typename Tuple, typename... Tuples>
auto
tuple_transform(Function &&function, Tuple &&tuple, Tuples &&...tuples) {
    static_assert(((std::tuple_size_v<std::remove_cvref_t<Tuple>> == std::tuple_size_v<std::remove_cvref_t<Tuples>>) &&...));
    return [&]<std::size_t... Idx>(std::index_sequence<Idx...>) {
        return std::make_tuple([&function, &tuple, &tuples...](auto I) { return function(std::get<I>(tuple), std::get<I>(tuples)...); }(std::integral_constant<std::size_t, Idx>{})...);
    }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tuple>>>());
}

static_assert(std::is_same_v<std::vector<int>, type_transform<std::vector, int>>);
static_assert(std::is_same_v<std::tuple<std::vector<int>, std::vector<float>>, type_transform<std::vector, std::tuple<int, float>>>);
static_assert(std::is_same_v<void, type_transform<std::vector, void>>);

} // namespace fair::meta

#endif // include guard
