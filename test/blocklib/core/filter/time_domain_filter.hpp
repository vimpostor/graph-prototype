#ifndef GRAPH_PROTOTYPE_TIME_DOMAIN_FILTER_HPP
#define GRAPH_PROTOTYPE_TIME_DOMAIN_FILTER_HPP

#include <history_buffer.hpp>
#include <node.hpp>
#include <numeric>

namespace gr::blocks::filter {

using namespace fair::graph;

/**
 * @brief Finite Impulse Response (FIR) filter class
 *
 * The transfer function of an FIR filter is given by:
 * H(z) = b[0] + b[1]*z^-1 + b[2]*z^-2 + ... + b[N]*z^-N
 *
 */
template<typename T>
    requires std::floating_point<T>
struct fir_filter : node<fir_filter<T>> {
    IN<T>             in;
    OUT<T>            out;
    std::vector<T>    b{}; // feedforward coefficients
    history_buffer<T> inputHistory{ 32 };

    void
    init(const property_map & /*old_settings*/, const property_map &new_settings) noexcept {
        if (new_settings.contains("b") && b.size() >= inputHistory.capacity()) {
            inputHistory = history_buffer<T>(std::bit_ceil(b.size()));
        }
    }

    constexpr T
    process_one(T input) noexcept {
        inputHistory.push_back(input);
        return std::inner_product(b.begin(), b.end(), inputHistory.rbegin(), static_cast<T>(0));
    }
};

enum class IIRForm {
    DF_I,  /// direct form I: preferred for fixed-point arithmetics (e.g. no overflow)
    DF_II, /// direct form II: preferred for floating-point arithmetics (less operations)
    DF_I_TRANSPOSED,
    DF_II_TRANSPOSED
};

/**
 * @brief Infinite Impulse Response (IIR) filter class
 *
 * b are the feed-forward coefficients (N.B. b[0] denoting the newest and n[-1] the previous sample)
 * a are the feedback coefficients
 *
 */
template<typename T, IIRForm form = std::is_floating_point_v<T> ? IIRForm::DF_II : IIRForm::DF_I>
    requires std::floating_point<T>
struct iir_filter : node<iir_filter<T, form>> {
    IN<T>             in;
    OUT<T>            out;
    std::vector<T>    b{ 1 }; // feedforward coefficients
    std::vector<T>    a{ 1 }; // feedback coefficients
    history_buffer<T> inputHistory{ 32 };
    history_buffer<T> outputHistory{ 32 };

    void
    init(const property_map & /*old_settings*/, const property_map &new_settings) noexcept {
        const auto new_size = std::max(a.size(), b.size());
        if ((new_settings.contains("b") || new_settings.contains("a")) && (new_size >= inputHistory.capacity() || new_size >= inputHistory.capacity())) {
            inputHistory  = history_buffer<T>(std::bit_ceil(new_size));
            outputHistory = history_buffer<T>(std::bit_ceil(new_size));
        }
    }

    [[nodiscard]] T
    process_one(T input) noexcept {
        if constexpr (form == IIRForm::DF_I) {
            // y[n] = b[0] * x[n]   + b[1] * x[n-1] + ... + b[N] * x[n-N]
            //      - a[1] * y[n-1] - a[2] * y[n-2] - ... - a[M] * y[n-M]
            inputHistory.push_back(input);
            const T output = std::inner_product(b.begin(), b.end(), inputHistory.rbegin(), static_cast<T>(0))       // feed-forward path
                           - std::inner_product(a.begin() + 1, a.end(), outputHistory.rbegin(), static_cast<T>(0)); // feedback path
            outputHistory.push_back(output);
            return output;
        } else if constexpr (form == IIRForm::DF_II) {
            // w[n] = x[n] - a[1] * w[n-1] - a[2] * w[n-2] - ... - a[M] * w[n-M]
            // y[n] =        b[0] * w[n]   + b[1] * w[n-1] + ... + b[N] * w[n-N]
            const T w = input - std::inner_product(a.begin() + 1, a.end(), inputHistory.rbegin(), T{ 0 });
            inputHistory.push_back(w);
            return std::inner_product(b.begin(), b.end(), inputHistory.rbegin(), T{ 0 });
        } else if constexpr (form == IIRForm::DF_I_TRANSPOSED) {
            // w_1[n] = x[n] - a[1] * w_2[n-1] - a[2] * w_2[n-2] - ... - a[M] * w_2[n-M]
            // y[n]   = b[0] * w_2[n] + b[1] * w_2[n-1] + ... + b[N] * w_2[n-N]
            T v0 = input - std::inner_product(a.begin() + 1, a.end(), outputHistory.rbegin(), static_cast<T>(0));
            outputHistory.push_back(v0);
            return std::inner_product(b.begin(), b.end(), outputHistory.rbegin(), static_cast<T>(0));
        } else if constexpr (form == IIRForm::DF_II_TRANSPOSED) {
            // y[n] = b_0*f[n] + \sum_(k=1)^N(b_k*f[n−k] − a_k*y[n−k])
            const T output = b[0] * input                                                                         //
                           + std::inner_product(b.begin() + 1, b.end(), inputHistory.rbegin(), static_cast<T>(0)) //
                           - std::inner_product(a.begin() + 1, a.end(), outputHistory.rbegin(), static_cast<T>(0));

            inputHistory.push_back(input);
            outputHistory.push_back(output);
            return output;
        }
    }
};

} // namespace gr::blocks::filter

ENABLE_REFLECTION_FOR_TEMPLATE_FULL((typename T), (gr::blocks::filter::fir_filter<T>), in, out, b);
ENABLE_REFLECTION_FOR_TEMPLATE_FULL((typename T, gr::blocks::filter::IIRForm form), (gr::blocks::filter::iir_filter<T, form>), in, out, b, a);

#endif // GRAPH_PROTOTYPE_TIME_DOMAIN_FILTER_HPP
