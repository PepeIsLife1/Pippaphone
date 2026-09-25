#include <cstdlib>
#include <cstring>

#include "testing.h"

std::vector<TestCase>& testRegistry() {
    static std::vector<TestCase> r;
    return r;
}
int g_failures = 0;

int main(int argc, char** argv) {
    const char* only = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (auto& t : testRegistry()) {
        if (only && !std::strstr(t.name, only)) continue;
        int before = g_failures;
        std::printf("%-40s", t.name);
        std::fflush(stdout);
        const char* seedEnv = std::getenv("PN_SEED");
        seedRandom(seedEnv ? uint32_t(std::strtoul(seedEnv, nullptr, 10)) : 12345u);
        t.fn();
        std::printf("%s\n", g_failures == before ? "ok" : "ERRORE");
        run++;
    }
    std::printf("\n%d test, %d controlli falliti\n", run, g_failures);
    return g_failures ? 1 : 0;
}
