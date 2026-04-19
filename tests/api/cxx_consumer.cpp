// tests/api/cxx_consumer.cpp
// API-07 compile-only test: prove include/spu94/spu94.h works through a C++
// consumer via the extern "C" wrapper in the header. Compiles under
// -std=c++11 -pedantic -Werror -Wall -Wextra; links against spu94_static so
// every Plan-01 public symbol's C linkage is exercised.
#include <spu94/spu94.h>
#include <cstdint>
#include <cstddef>

int main() {
    std::size_t s = spu94_state_size();
    (void)s;

    spu94_state *p = spu94_init(nullptr, 0, nullptr, 0);
    (void)p;
    spu94_reset(nullptr);
    spu94_destroy(nullptr);

    spu94_result_t r0 = SPU94_OK;
    spu94_result_t r1 = SPU94_CLAMPED;
    spu94_result_t r2 = SPU94_UNKNOWN_REG;
    spu94_result_t r3 = SPU94_TYPE_MISMATCH;
    (void)r0; (void)r1; (void)r2; (void)r3;

    std::size_t bound = static_cast<std::size_t>(SPU94_STATE_SIZE_MAX);
    std::size_t align = static_cast<std::size_t>(SPU94_STATE_ALIGN_MAX);
    (void)bound; (void)align;

    return 0;
}
