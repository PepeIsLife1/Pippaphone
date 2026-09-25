// Mini-framework di test: TEST(nome) { CHECK(...); }
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

std::vector<TestCase>& testRegistry();
extern int g_failures;

struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> fn) { testRegistry().push_back({name, fn}); }
};

#define TEST(name)                                         \
    static void test_##name();                             \
    static TestRegistrar reg_##name(#name, test_##name);   \
    static void test_##name()

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::printf("    FALLITO %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        auto va = (a);                                                                              \
        auto vb = (b);                                                                              \
        if (!(va == vb)) {                                                                          \
            std::printf("    FALLITO %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b);               \
            g_failures++;                                                                           \
        }                                                                                           \
    } while (0)

// Seme del generatore casuale dei test (deterministico).
void seedRandom(uint32_t seed);
