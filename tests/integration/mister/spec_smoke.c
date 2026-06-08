/* Smoke test: proves the test toolchain compiles, links, and runs.
 * The real assertion here is structural — if this binary builds and executes,
 * the Criterion harness and the standalone Makefile are wired correctly. */
#include <criterion/criterion.h>

Test(smoke, harness_runs)
{
   cr_assert(1, "the test harness builds, links against Criterion, and runs");
}
