// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 ALHROOBIX
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace xbox {

// ---------------------------------------------------------------------------
// Ok / Err tag types for constructing Results
// ---------------------------------------------------------------------------
template <typename T>
struct Ok {
    T value;
    explicit Ok(T v) : value(std::move(v)) {}
};

template <>
struct Ok<void> {
    Ok() = default;
};

struct OkTag {};
inline constexpr OkTag ok_tag{};

template <typename E>
struct Err {
    E value;
    explicit Err(E e) : value(std::move(e)) {}
};

struct ErrTag {};
inline constexpr ErrTag err_tag{};

template <typename E>
inline Err<E> err(E e) { return Err<E>(std::move(e)); }

template <typename E>
inline Err<E> make_err(E e) { return Err<E>(std::move(e)); }

// ---------------------------------------------------------------------------
// Result<T, E> - holds either a success value of type T or an error E.
// Specialization for T = void provided below.
// ---------------------------------------------------------------------------
template <typename T, typename E>
class Result {
public:
    static_assert(!std::is_same_v<T, void>, "Use Result<void, E> specialization");

    // Constructors from Ok / Err
    Result(Ok<T> o) : storage_(std::in_place_index<0>, std::move(o.value)) {}
    Result(Err<E> e) : storage_(std::in_place_index<1>, std::move(e.value)) {}

    // Implicit construction from a value of T (success).
    // NOTE: We intentionally do NOT provide an implicit constructor from E -
    // use Err{e} explicitly. This avoids ambiguity when E has its own
    // conversion operator to Result<T, E>.
    template <typename U = T, std::enable_if_t<!std::is_same_v<U, void>, int> = 0>
    Result(T v) : storage_(std::in_place_index<0>, std::move(v)) {}

    // Allow implicit construction from Err<E2> for compatible error types.
    // This enables `return XBOX_*_ERROR(...)` to work where XBOX_*_ERROR
    // returns Error and the function returns Result<T, Error>.
    // Note: we use Err<E> as the intermediary to avoid ambiguity with T.
    template <typename E2, std::enable_if_t<std::is_same_v<std::decay_t<E2>, E>, int> = 0>
    Result(E2 e) : storage_(std::in_place_index<1>, std::move(e)) {}

    [[nodiscard]] bool is_ok()   const noexcept { return storage_.index() == 0; }
    [[nodiscard]] bool is_err()  const noexcept { return storage_.index() == 1; }
    [[nodiscard]] explicit operator bool() const noexcept { return is_ok(); }

    // Unchecked accessors (UB if wrong state) - prefer unwrap()
    [[nodiscard]] T& value() & {
        assert(is_ok() && "Result::value() called on Err");
        return std::get<0>(storage_);
    }
    [[nodiscard]] const T& value() const & {
        assert(is_ok() && "Result::value() called on Err");
        return std::get<0>(storage_);
    }
    [[nodiscard]] T&& value() && {
        assert(is_ok() && "Result::value() called on Err");
        return std::get<0>(std::move(storage_));
    }

    [[nodiscard]] E& error() & {
        assert(is_err() && "Result::error() called on Ok");
        return std::get<1>(storage_);
    }
    [[nodiscard]] const E& error() const & {
        assert(is_err() && "Result::error() called on Ok");
        return std::get<1>(storage_);
    }

    // Checked unwrap - returns T on success, aborts on error (use in tests)
    [[nodiscard]] T unwrap() && {
        assert(is_ok() && "Result::unwrap() called on Err");
        return std::get<0>(std::move(storage_));
    }
    [[nodiscard]] T unwrap() & {
        assert(is_ok() && "Result::unwrap() called on Err");
        return std::get<0>(storage_);
    }

    // unwrap_or / unwrap_or_else
    [[nodiscard]] T unwrap_or(T fallback) const & {
        return is_ok() ? std::get<0>(storage_) : std::move(fallback);
    }
    [[nodiscard]] T unwrap_or(T fallback) && {
        return is_ok() ? std::get<0>(std::move(storage_)) : std::move(fallback);
    }
    template <typename F>
    [[nodiscard]] T unwrap_or_else(F&& f) && {
        return is_ok() ? std::get<0>(std::move(storage_)) : std::forward<F>(f)();
    }

    // map: T -> U, propagates error
    template <typename F>
    auto map(F&& f) & {
        using U = std::invoke_result_t<F, T&>;
        if constexpr (std::is_void_v<U>) {
            if (is_ok()) { std::forward<F>(f)(value()); return Result<void, E>{}; }
            return Result<void, E>{Err<E>{error()}};
        } else {
            if (is_ok()) return Result<U, E>{Ok<U>{std::forward<F>(f)(value())}};
            return Result<U, E>{Err<E>{error()}};
        }
    }
    template <typename F>
    auto map(F&& f) const & {
        using U = std::invoke_result_t<F, const T&>;
        if constexpr (std::is_void_v<U>) {
            if (is_ok()) { std::forward<F>(f)(value()); return Result<void, E>{}; }
            return Result<void, E>{Err<E>{error()}};
        } else {
            if (is_ok()) return Result<U, E>{Ok<U>{std::forward<F>(f)(value())}};
            return Result<U, E>{Err<E>{error()}};
        }
    }
    template <typename F>
    auto map(F&& f) && {
        using U = std::invoke_result_t<F, T&&>;
        if constexpr (std::is_void_v<U>) {
            if (is_ok()) { std::forward<F>(f)(std::move(value())); return Result<void, E>{}; }
            return Result<void, E>{Err<E>{std::move(error())}};
        } else {
            if (is_ok()) return Result<U, E>{Ok<U>{std::forward<F>(f)(std::move(value()))}};
            return Result<U, E>{Err<E>{std::move(error())}};
        }
    }

    // map_err: E -> E2, propagates value
    template <typename F>
    auto map_err(F&& f) & {
        using E2 = std::invoke_result_t<F, E&>;
        if (is_err()) return Result<T, E2>{Err<E2>{std::forward<F>(f)(error())}};
        return Result<T, E2>{Ok<T>{value()}};
    }

    // and_then: T -> Result<U, E> (monadic bind)
    template <typename F>
    auto and_then(F&& f) & {
        using R = std::invoke_result_t<F, T&>;
        if (is_ok()) return std::forward<F>(f)(value());
        return R{Err<typename R::error_type>{error()}};
    }
    template <typename F>
    auto and_then(F&& f) && {
        using R = std::invoke_result_t<F, T&&>;
        if (is_ok()) return std::forward<F>(f)(std::move(value()));
        return R{Err<typename R::error_type>{std::move(error())}};
    }

private:
    std::variant<T, E> storage_;
};

// ---------------------------------------------------------------------------
// Specialization for T = void
// ---------------------------------------------------------------------------
template <typename E>
class Result<void, E> {
public:
    Result() : ok_(true) {}                          // implicit success
    Result(Ok<void>) : ok_(true) {}
    Result(Err<E> e) : ok_(false), err_(std::move(e.value)) {}

    // Allow implicit construction from E for `return XBOX_*_ERROR(...)` style.
    template <typename E2, std::enable_if_t<std::is_same_v<std::decay_t<E2>, E>, int> = 0>
    Result(E2 e) : ok_(false), err_(std::move(e)) {}

    [[nodiscard]] bool is_ok()  const noexcept { return ok_; }
    [[nodiscard]] bool is_err() const noexcept { return !ok_; }
    [[nodiscard]] explicit operator bool() const noexcept { return is_ok(); }

    void value() const { assert(ok_ && "Result<void>::value() called on Err"); }
    void unwrap() const { assert(ok_ && "Result<void>::unwrap() called on Err"); }

    [[nodiscard]] const E& error() const {
        assert(!ok_ && "Result<void>::error() called on Ok");
        return *err_;
    }

private:
    bool ok_{true};
    // Use optional so E can be incomplete at template instantiation time.
    // (Required because Result<void, Error> is used in thread_pool.hpp before
    //  errors.hpp is included.)
    std::optional<E> err_{};
};

// ---------------------------------------------------------------------------
// Aliases for the most common error types
// ---------------------------------------------------------------------------
// Defined in errors.hpp - forward declaration
class Error;
template <typename T> using ResultT = Result<T, Error>;

} // namespace xbox
