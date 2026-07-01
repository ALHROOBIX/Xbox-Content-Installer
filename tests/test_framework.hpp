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

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace xbox::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
    const char* file;
    int line;
};

class Registry {
public:
    static Registry& instance() {
        static Registry r;
        return r;
    }

    void add(std::string name, std::function<void()> fn, const char* file, int line) {
        tests_.push_back({std::move(name), std::move(fn), file, line});
    }

    int run_all() {
        int passed = 0;
        int failed = 0;
        std::cout << "Running " << tests_.size() << " tests...\n";
        for (const auto& t : tests_) {
            std::cout << "  [RUN ] " << t.name << " ... ";
            std::cout.flush();
            try {
                t.fn();
                std::cout << "PASS\n";
                ++passed;
            } catch (const std::exception& e) {
                std::cout << "FAIL\n";
                std::cout << "    " << e.what() << "\n";
                ++failed;
            } catch (...) {
                std::cout << "FAIL (unknown exception)\n";
                ++failed;
            }
        }
        std::cout << "\n" << passed << " passed, " << failed << " failed\n";
        return failed > 0 ? 1 : 0;
    }

private:
    std::vector<TestCase> tests_;
};

// Helper to register a test at static init time
struct Registrar {
    Registrar(const char* name, std::function<void()> fn, const char* file, int line) {
        Registry::instance().add(name, std::move(fn), file, line);
    }
};

// Assertion failure exception
class AssertionFailure : public std::exception {
public:
    AssertionFailure(std::string msg, const char* file, int line)
        : msg_(std::move(msg)), file_(file), line_(line) {
        full_msg_ = std::string(file_) + ":" + std::to_string(line_) + ": " + msg_;
    }
    const char* what() const noexcept override { return full_msg_.c_str(); }
private:
    std::string msg_;
    const char* file_;
    int line_;
    std::string full_msg_;
};

} // namespace xbox::test

// ----- Macros -----

#define XBOX_TEST_NAME(name) xbox_test_##name

#define TEST(name) \
    static void XBOX_TEST_NAME(name)(); \
    static ::xbox::test::Registrar registrar_##name( \
        #name, XBOX_TEST_NAME(name), __FILE__, __LINE__); \
    static void XBOX_TEST_NAME(name)()

#define EXPECT_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (!(_a == _b)) { \
        throw ::xbox::test::AssertionFailure( \
            "EXPECT_EQ failed: " #a " != " #b, __FILE__, __LINE__); \
    } \
} while (0)

#define EXPECT_NE(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (!(_a != _b)) { \
        throw ::xbox::test::AssertionFailure( \
            "EXPECT_NE failed: " #a " == " #b, __FILE__, __LINE__); \
    } \
} while (0)

#define EXPECT_LT(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (!(_a < _b)) { \
        throw ::xbox::test::AssertionFailure( \
            "EXPECT_LT failed: " #a " >= " #b, __FILE__, __LINE__); \
    } \
} while (0)

#define EXPECT_LE(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (!(_a <= _b)) { \
        throw ::xbox::test::AssertionFailure( \
            "EXPECT_LE failed: " #a " > " #b, __FILE__, __LINE__); \
    } \
} while (0)

#define EXPECT_GT(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (!(_a > _b)) { \
        throw ::xbox::test::AssertionFailure( \
            "EXPECT_GT failed: " #a " <= " #b, __FILE__, __LINE__); \
    } \
} while (0)

#define EXPECT_GE(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (!(_a >= _b)) { \
        throw ::xbox::test::AssertionFailure( \
            "EXPECT_GE failed: " #a " < " #b, __FILE__, __LINE__); \
    } \
} while (0)

#define EXPECT_TRUE(c) do { \
    if (!(c)) { \
        throw ::xbox::test::AssertionFailure( \
            "EXPECT_TRUE failed: " #c, __FILE__, __LINE__); \
    } \
} while (0)

#define EXPECT_FALSE(c) do { \
    if (c) { \
        throw ::xbox::test::AssertionFailure( \
            "EXPECT_FALSE failed: " #c, __FILE__, __LINE__); \
    } \
} while (0)

#define ASSERT_TRUE(c) EXPECT_TRUE(c)
#define ASSERT_FALSE(c) EXPECT_FALSE(c)
#define ASSERT_EQ(a, b) EXPECT_EQ(a, b)
#define ASSERT_NE(a, b) EXPECT_NE(a, b)

#define EXPECT_STREQ(a, b) do { \
    if (std::string(a) != std::string(b)) { \
        throw ::xbox::test::AssertionFailure( \
            std::string("EXPECT_STREQ failed: \"") + (a) + "\" != \"" + (b) + "\"", \
            __FILE__, __LINE__); \
    } \
} while (0)

#define EXPECT_NEAR(a, b, eps) do { \
    auto _a = (a); auto _b = (b); auto _e = (eps); \
    if (std::abs(_a - _b) > _e) { \
        throw ::xbox::test::AssertionFailure( \
            "EXPECT_NEAR failed", __FILE__, __LINE__); \
    } \
} while (0)
