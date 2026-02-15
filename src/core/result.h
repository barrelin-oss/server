#pragma once

// result.h
// Result type for error handling without exceptions
// Similar to Rust's Result<T, E> or std::expected (C++23)

#include <variant>
#include <string>
#include <stdexcept>
#include <utility>
#include <type_traits>
#include <functional>

namespace hb
{

// Tag types for in-place construction
struct ok_tag_t
{
};
struct err_tag_t
{
};
inline constexpr ok_tag_t ok_tag{};
inline constexpr err_tag_t err_tag{};

// Result type: holds either a value T or an error E
template<typename T, typename E = std::string> class result
{
public:
    using value_type = T;
    using error_type = E;

    // Default constructor (if T is default constructible, creates ok value)
    result()
        requires std::is_default_constructible_v<T>
        : data_(std::in_place_index<0>, T{})
    {
    }

    // Construct with ok value
    result(ok_tag_t, T value) : data_(std::in_place_index<0>, std::move(value)) {}

    // Construct with error
    result(err_tag_t, E error) : data_(std::in_place_index<1>, std::move(error)) {}

    // Implicit conversion from T (creates ok)
    result(T value) : data_(std::in_place_index<0>, std::move(value)) {}

    // Static factory methods
    [[nodiscard]] static auto ok(T value) -> result { return result(ok_tag, std::move(value)); }

    [[nodiscard]] static auto err(E error) -> result { return result(err_tag, std::move(error)); }

    // Check if result is ok
    [[nodiscard]] auto is_ok() const noexcept -> bool { return data_.index() == 0; }

    // Check if result is error
    [[nodiscard]] auto is_err() const noexcept -> bool { return data_.index() == 1; }

    // Explicit bool conversion (true if ok)
    explicit operator bool() const noexcept { return is_ok(); }

    // Get value (throws if error)
    [[nodiscard]] auto value() & -> T&
    {
        if (is_err())
        {
            throw std::runtime_error("result::value() called on error result");
        }
        return std::get<0>(data_);
    }

    [[nodiscard]] auto value() const& -> const T&
    {
        if (is_err())
        {
            throw std::runtime_error("result::value() called on error result");
        }
        return std::get<0>(data_);
    }

    [[nodiscard]] auto value() && -> T&&
    {
        if (is_err())
        {
            throw std::runtime_error("result::value() called on error result");
        }
        return std::get<0>(std::move(data_));
    }

    // Get error (throws if ok)
    [[nodiscard]] auto error() & -> E&
    {
        if (is_ok())
        {
            throw std::runtime_error("result::error() called on ok result");
        }
        return std::get<1>(data_);
    }

    [[nodiscard]] auto error() const& -> const E&
    {
        if (is_ok())
        {
            throw std::runtime_error("result::error() called on ok result");
        }
        return std::get<1>(data_);
    }

    [[nodiscard]] auto error() && -> E&&
    {
        if (is_ok())
        {
            throw std::runtime_error("result::error() called on ok result");
        }
        return std::get<1>(std::move(data_));
    }

    // Get value or default
    [[nodiscard]] auto value_or(T default_value) const& -> T
    {
        return is_ok() ? std::get<0>(data_) : std::move(default_value);
    }

    [[nodiscard]] auto value_or(T default_value) && -> T
    {
        return is_ok() ? std::get<0>(std::move(data_)) : std::move(default_value);
    }

    // Get value or compute from function
    template<typename F> [[nodiscard]] auto value_or_else(F&& f) const& -> T
    {
        return is_ok() ? std::get<0>(data_) : std::invoke(std::forward<F>(f));
    }

    // Map: transform value if ok
    template<typename F> [[nodiscard]] auto map(F&& f) const& -> result<std::invoke_result_t<F, const T&>, E>
    {
        using U = std::invoke_result_t<F, const T&>;
        if (is_ok())
        {
            return result<U, E>::ok(std::invoke(std::forward<F>(f), std::get<0>(data_)));
        }
        return result<U, E>::err(std::get<1>(data_));
    }

    template<typename F> [[nodiscard]] auto map(F&& f) && -> result<std::invoke_result_t<F, T&&>, E>
    {
        using U = std::invoke_result_t<F, T&&>;
        if (is_ok())
        {
            return result<U, E>::ok(std::invoke(std::forward<F>(f), std::get<0>(std::move(data_))));
        }
        return result<U, E>::err(std::get<1>(std::move(data_)));
    }

    // Map error: transform error if err
    template<typename F> [[nodiscard]] auto map_err(F&& f) const& -> result<T, std::invoke_result_t<F, const E&>>
    {
        using U = std::invoke_result_t<F, const E&>;
        if (is_err())
        {
            return result<T, U>::err(std::invoke(std::forward<F>(f), std::get<1>(data_)));
        }
        return result<T, U>::ok(std::get<0>(data_));
    }

    // And then: chain operations
    template<typename F> [[nodiscard]] auto and_then(F&& f) const& -> std::invoke_result_t<F, const T&>
    {
        if (is_ok())
        {
            return std::invoke(std::forward<F>(f), std::get<0>(data_));
        }
        return std::invoke_result_t<F, const T&>::err(std::get<1>(data_));
    }

    template<typename F> [[nodiscard]] auto and_then(F&& f) && -> std::invoke_result_t<F, T&&>
    {
        if (is_ok())
        {
            return std::invoke(std::forward<F>(f), std::get<0>(std::move(data_)));
        }
        return std::invoke_result_t<F, T&&>::err(std::get<1>(std::move(data_)));
    }

    // Or else: provide alternative on error
    template<typename F> [[nodiscard]] auto or_else(F&& f) const& -> result
    {
        if (is_err())
        {
            return std::invoke(std::forward<F>(f), std::get<1>(data_));
        }
        return *this;
    }

    // Unwrap: get value or throw with custom message
    [[nodiscard]] auto expect(std::string_view msg) & -> T&
    {
        if (is_err())
        {
            throw std::runtime_error(std::string(msg));
        }
        return std::get<0>(data_);
    }

    [[nodiscard]] auto expect(std::string_view msg) const& -> const T&
    {
        if (is_err())
        {
            throw std::runtime_error(std::string(msg));
        }
        return std::get<0>(data_);
    }

    [[nodiscard]] auto expect(std::string_view msg) && -> T&&
    {
        if (is_err())
        {
            throw std::runtime_error(std::string(msg));
        }
        return std::get<0>(std::move(data_));
    }

private:
    std::variant<T, E> data_;
};

// Specialization for void value type
template<typename E> class result<void, E>
{
public:
    using value_type = void;
    using error_type = E;

    // Default constructor creates ok
    result() : has_error_(false), error_() {}

    // Construct with error
    result(err_tag_t, E error) : has_error_(true), error_(std::move(error)) {}

    // Static factory methods
    [[nodiscard]] static auto ok() -> result { return result(); }

    [[nodiscard]] static auto err(E error) -> result { return result(err_tag, std::move(error)); }

    [[nodiscard]] auto is_ok() const noexcept -> bool { return !has_error_; }

    [[nodiscard]] auto is_err() const noexcept -> bool { return has_error_; }

    explicit operator bool() const noexcept { return is_ok(); }

    [[nodiscard]] auto error() const& -> const E&
    {
        if (!has_error_)
        {
            throw std::runtime_error("result::error() called on ok result");
        }
        return error_;
    }

    [[nodiscard]] auto error() && -> E&&
    {
        if (!has_error_)
        {
            throw std::runtime_error("result::error() called on ok result");
        }
        return std::move(error_);
    }

    void expect(std::string_view msg) const
    {
        if (has_error_)
        {
            throw std::runtime_error(std::string(msg));
        }
    }

private:
    bool has_error_;
    E error_;
};

// Helper function to create ok result
template<typename T> [[nodiscard]] auto make_ok(T value) -> result<T, std::string>
{
    return result<T, std::string>::ok(std::move(value));
}

// Helper function to create error result
template<typename T = void, typename E = std::string> [[nodiscard]] auto make_err(E error) -> result<T, E>
{
    return result<T, E>::err(std::move(error));
}

} // namespace hb
