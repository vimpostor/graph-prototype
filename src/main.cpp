#include <array>
#include <fmt/core.h>

#include <cassert>

#include <graph.hpp>

namespace fg = fair::graph;

template<typename T>
class count_source : public fg::node<count_source<T>, fg::OUT<T, 0, std::numeric_limits<std::size_t>::max(), "random">> {
public:
    constexpr T
    process_one() {
        return 42;
    }
};

template<typename T>
class expect_sink : public fg::node<expect_sink<T>, fg::IN<T, 0, std::numeric_limits<std::size_t>::max(), "sink">> {
public:
    void
    process_one(T value) {
        std::cout << value << std::endl;
    }
};

template<typename T, T Scale, typename R = decltype(std::declval<T>() * std::declval<T>())>
class scale : public fg::node<scale<T, Scale, R>, fg::IN<T, 0, std::numeric_limits<std::size_t>::max(), "original">, fg::OUT<R, 0, std::numeric_limits<std::size_t>::max(), "scaled">> {
public:
    template<fair::meta::t_or_simd<T> V>
    [[nodiscard]] constexpr auto
    process_one(V a) const noexcept {
        return a * Scale;
    }
};

template<typename T, typename R = decltype(std::declval<T>() + std::declval<T>())>
class adder : public fg::node<adder<T>, fg::IN<T, 0, std::numeric_limits<std::size_t>::max(), "addend0">, fg::IN<T, 0, std::numeric_limits<std::size_t>::max(), "addend1">, fg::OUT<R, 0, std::numeric_limits<std::size_t>::max(), "sum">> {
public:
    template<fair::meta::t_or_simd<T> V>
    [[nodiscard]] constexpr auto
    process_one(V a, V b) const noexcept {
        return a + b;
    }
};

template<typename T, std::size_t Count = 2>
class duplicate : public fg::node<duplicate<T, Count>, fair::meta::typelist<fg::IN<T, 0, std::numeric_limits<std::size_t>::max(), "in">>, fg::repeated_ports<Count, T, "out", fg::port_type_t::STREAM, fg::port_direction_t::OUTPUT>> {
    using base = fg::node<duplicate<T, Count>, fair::meta::typelist<fg::IN<T, 0, std::numeric_limits<std::size_t>::max(), "in">>, fg::repeated_ports<Count, T, "out", fg::port_type_t::STREAM, fg::port_direction_t::OUTPUT>>;

public:
    using return_type = typename fg::traits::node::return_type<base>;

    [[nodiscard]] constexpr return_type
    process_one(T a) const noexcept {
        return [&a]<std::size_t... Is>(std::index_sequence<Is...>) { return std::make_tuple(((void) Is, a)...); }
        (std::make_index_sequence<Count>());
    }
};

template<typename T, std::size_t Depth>
    requires(Depth > 0)
class delay : public fg::node<delay<T, Depth>, fg::IN<T, 0, std::numeric_limits<std::size_t>::max(), "in">, fg::OUT<T, 0, std::numeric_limits<std::size_t>::max(), "out">> {
    std::array<T, Depth> buffer = {};
    int                  pos    = 0;

public:
    [[nodiscard]] constexpr T
    process_one(T in) noexcept {
        T ret       = buffer[pos];
        buffer[pos] = in;
        if (pos == Depth - 1) {
            pos = 0;
        } else {
            ++pos;
        }
        return ret;
    }
};

int
main() {
    using fg::merge;
    using fg::merge_by_index;

    {
        // declare flow-graph: 2 x in -> adder -> scale-by-2 -> scale-by-minus1 -> output
        auto merged = merge_by_index<0, 0>(scale<int, -1>(), merge_by_index<0, 0>(scale<int, 2>(), adder<int>()));

        // execute graph
        std::array<int, 4> a = { 1, 2, 3, 4 };
        std::array<int, 4> b = { 10, 10, 10, 10 };

        int                r = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            r += merged.process_one(a[i], b[i]);
        }

        fmt::print("Result of graph execution: {}\n", r);

        assert(r == 20);
    }

    {
        auto merged = merge_by_index<0, 0>(duplicate<int, 2>(), scale<int, 2>());

        // execute graph
        std::array<int, 4> a = { 1, 2, 3, 4 };

        for (std::size_t i = 0; i < 4; ++i) {
            auto tuple    = merged.process_one(a[i]);
            auto [r1, r2] = tuple;
            fmt::print("{} {} \n", r1, r2);
        }
    }

    {
        auto merged = merge<"scaled", "addend1">(scale<int, 2>(), adder<int>());

        // execute graph
        std::array<int, 4> a = { 1, 2, 3, 4 };
        std::array<int, 4> b = { 10, 10, 10, 10 };

        int                r = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            r += merged.process_one(a[i], b[i]);
        }

        fmt::print("Result of graph execution: {}\n", r);

        assert(r == 60);
    }

    {
        auto merged = merge_by_index<1, 0>(merge_by_index<0, 0>(duplicate<int, 4>(), scale<int, 2>()), scale<int, 2>());

        // execute graph
        std::array<int, 4> a = { 1, 2, 3, 4 };

        for (std::size_t i = 0; i < 4; ++i) {
            auto tuple            = merged.process_one(a[i]);
            auto [r1, r2, r3, r4] = tuple;
            fmt::print("{} {} {} {} \n", r1, r2, r3, r4);
        }
    }

    { auto delayed = delay<int, 2>{}; }

    {
        auto random = count_source<int>{};

        auto merged = merge_by_index<0, 0>(std::move(random), expect_sink<int>());
        merged.process_one();
    }

    {
        auto random = count_source<int>{};

        auto merged = merge<"random", "original">(std::move(random), scale<int, 2>());
        merged.process_one();
    }
}
