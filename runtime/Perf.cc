/* Copyright (c) 2016 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

// This program contains a collection of low-level performance measurements
// for FastLogger, which can be run either individually or altogether.  These
// tests measure performance in a single stand-alone process.
// Invoke the program like this:
//
//     Perf test1 test2 ...
//
// test1 and test2 are the names of individual performance measurements to
// run.  If no test names are provided then all of the performance tests
// are run.
//
// To add a new test:
// * Write a function that implements the test.  Use existing test functions
//   as a guideline, and be sure to generate output in the same form as
//   other tests.
// * Create a new entry for the test in the #tests table.

#include <cstdio>
#include <cstring>
#include <map>

#include <unistd.h>
#include <sched.h>
#include <stdlib.h>
#include <syscall.h>

#include "Cycles.h"
#include "PerfHelper.h"
#include "Util.h"

using namespace PerfUtils;
/**
 * Ask the operating system to pin the current thread to a given CPU.
 *
 * \param cpu
 *      Indicates the desired CPU and hyperthread; low order 2 bits
 *      specify CPU, next bit specifies hyperthread.
 */
void bindThreadToCpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    sched_setaffinity((pid_t)syscall(SYS_gettid), sizeof(set), &set);
}

/*
 * This function just discards its argument. It's used to make it
 * appear that data is used,  so that the compiler won't optimize
 * away the code we're trying to measure.
 *
 * \param value
 *      Pointer to arbitrary value; it's discarded.
 */
void discard(void* value) {
    int x = *reinterpret_cast<int*>(value);
    if (x == 0x43924776) {
        printf("Value was 0x%x\n", x);
    }
}

//----------------------------------------------------------------------
// Test functions start here
//----------------------------------------------------------------------

// Measure the cost of a 32-bit divide. Divides don't take a constant
// number of cycles. Values were chosen here semi-randomly to depict a
// fairly expensive scenario. Someone with fancy ALU knowledge could
// probably pick worse values.
double div32()
{
    int count = 1000000;
    uint64_t start = Cycles::rdtsc();
    // NB: Expect an x86 processor exception is there's overflow.
    uint32_t numeratorHi = 0xa5a5a5a5U;
    uint32_t numeratorLo = 0x55aa55aaU;
    uint32_t divisor = 0xaa55aa55U;
    uint32_t quotient;
    uint32_t remainder;
    for (int i = 0; i < count; i++) {
        __asm__ __volatile__("div %4" :
                             "=a"(quotient), "=d"(remainder) :
                             "a"(numeratorLo), "d"(numeratorHi), "r"(divisor) :
                             "cc");
    }
    uint64_t stop = Cycles::rdtsc();
    return Cycles::toSeconds(stop - start)/count;
}

// Measure the cost of a 64-bit divide. Divides don't take a constant
// number of cycles. Values were chosen here semi-randomly to depict a
// fairly expensive scenario. Someone with fancy ALU knowledge could
// probably pick worse values.
double div64()
{
    int count = 1000000;
    // NB: Expect an x86 processor exception is there's overflow.
    uint64_t start = Cycles::rdtsc();
    uint64_t numeratorHi = 0x5a5a5a5a5a5UL;
    uint64_t numeratorLo = 0x55aa55aa55aa55aaUL;
    uint64_t divisor = 0xaa55aa55aa55aa55UL;
    uint64_t quotient;
    uint64_t remainder;
    for (int i = 0; i < count; i++) {
        __asm__ __volatile__("divq %4" :
                             "=a"(quotient), "=d"(remainder) :
                             "a"(numeratorLo), "d"(numeratorHi), "r"(divisor) :
                             "cc");
    }
    uint64_t stop = Cycles::rdtsc();
    return Cycles::toSeconds(stop - start)/count;
}

// Measure the cost of calling a non-inlined function.
double functionCall()
{
    int count = 1000000;
    uint64_t x = 0;
    uint64_t start = Cycles::rdtsc();
    for (int i = 0; i < count; i++) {
        PerfHelper::plusOne(x);
    }
    uint64_t stop = Cycles::rdtsc();

    discard(&count);
    return Cycles::toSeconds(stop - start)/(count);
}

double functionDereference()
{
    const int count = 1000000;
    char indecies[count];

    srand(0);
    for (int i = 0; i < count; ++i)
        indecies[i] = static_cast<char>(rand() % 50);

    uint64_t start = Cycles::rdtsc();
    for (int i = 0; i < count; ++i) {
        PerfHelper::functionArray[indecies[i]]();
    }
    uint64_t stop = Cycles::rdtsc();

    return Cycles::toSeconds(stop - start)/count;
}

// Measure the time to create and delete an entry in a small
// map.
double mapCreate()
{
    srand(0);

    // Generate an array of random keys that will be used to lookup
    // entries in the map.
    int numKeys = 20;
    uint64_t keys[numKeys];
    for (int i = 0; i < numKeys; i++) {
        keys[i] = rand();
    }

    int count = 10000;
    uint64_t start = Cycles::rdtsc();
    for (int i = 0; i < count; i += 5) {
        std::map<uint64_t, uint64_t> map;
        for (int j = 0; j < numKeys; j++) {
            map[keys[j]] = 1000+j;
        }
        for (int j = 0; j < numKeys; j++) {
            map.erase(keys[j]);
        }
    }
    uint64_t stop = Cycles::rdtsc();
    return Cycles::toSeconds(stop - start)/(count * numKeys);
}

// Measure the time to lookup a random element in a small map.
double mapLookup()
{
    std::map<uint64_t, uint64_t> map;
    srand(0);

    // Generate an array of random keys that will be used to lookup
    // entries in the map.
    int numKeys = 20;
    uint64_t keys[numKeys];
    for (int i = 0; i < numKeys; i++) {
        keys[i] = rand();
        map[keys[i]] = 12345;
    }

    int count = 100000;
    uint64_t sum = 0;
    uint64_t start = Cycles::rdtsc();
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < numKeys; j++) {
            sum += map[keys[j]];
        }
    }
    uint64_t stop = Cycles::rdtsc();
    return Cycles::toSeconds(stop - start)/(count*numKeys);
}

// Measure the cost of copying a given number of bytes with memcpy.
double memcpyShared(int cpySize, bool coldSrc, bool coldDst)
{
    int count = 1000000;
    uint32_t src[count], dst[count];
    int bufSize = 1000000000; // 1GB buffer
    char *buf = static_cast<char*>(malloc(bufSize));

    uint32_t bound = (bufSize - cpySize);
    for (int i = 0; i < count; i++) {
        src[i] = (coldSrc) ? (rand() % bound) : 0;
        dst[i] = (coldDst) ? (rand() % bound) : 0;
    }

    uint64_t start = Cycles::rdtsc();
    for (int i = 0; i < count; i++) {
        std::memcpy((buf + dst[i]),
                    (buf + src[i]),
                    cpySize);
    }
    uint64_t stop = Cycles::rdtsc();

    free(buf);
    return Cycles::toSeconds(stop - start)/(count);
}

double memcpyCached100()
{
    return memcpyShared(100, false, false);
}

double memcpyCached1000()
{
    return memcpyShared(1000, false, false);
}

double memcpyCachedDst100()
{
    return memcpyShared(100, true, false);
}

double memcpyCachedDst1000()
{
    return memcpyShared(1000, true, false);
}

double memcpyCold100()
{
    return memcpyShared(100, true, true);
}

double memcpyCold1000()
{
    return memcpyShared(1000, true, true);
}

// Measure the cost of the Cylcles::toNanoseconds method.
double perfCyclesToNanoseconds()
{
    int count = 1000000;
    uint64_t total = 0;
    uint64_t cycles = 994261;

    uint64_t start = Cycles::rdtsc();
    for (int i = 0; i < count; i++) {
        total += Cycles::toNanoseconds(cycles);
    }
    uint64_t stop = Cycles::rdtsc();

    return Cycles::toSeconds(stop - start)/count;
}

// Measure the cost of the Cycles::toSeconds method.
double perfCyclesToSeconds()
{
    int count = 1000000;
    double total = 0;
    uint64_t cycles = 994261;

    uint64_t start = Cycles::rdtsc();
    for (int i = 0; i < count; i++) {
        total += Cycles::toSeconds(cycles);
    }
    uint64_t stop = Cycles::rdtsc();

    return Cycles::toSeconds(stop - start)/count;
}

// Measure the cost of reading the fine-grain cycle counter.
double rdtscTest()
{
    int count = 1000000;
    uint64_t start = Cycles::rdtsc();
    uint64_t total = 0;
    for (int i = 0; i < count; i++) {
        total += Cycles::rdtsc();
    }
    uint64_t stop = Cycles::rdtsc();
    return Cycles::toSeconds(stop - start)/count;
}

// Measure the cost of cpuid
double serialize() {
    int count = 1000000;
    uint64_t start = Cycles::rdtsc();
    for (int i = 0; i < count; i++) {
        Util::serialize();
    }
    uint64_t stop = Cycles::rdtsc();
    return Cycles::toSeconds(stop - start)/count;
}

// The following struct and table define each performance test in terms of
// a string name and a function that implements the test.
struct TestInfo {
    const char* name;             // Name of the performance test; this is
                                  // what gets typed on the command line to
                                  // run the test.
    double (*func)();             // Function that implements the test;
                                  // returns the time (in seconds) for each
                                  // iteration of that test.
    const char *description;      // Short description of this test (not more
                                  // than about 40 characters, so the entire
                                  // test output fits on a single line).
};
TestInfo tests[] = {
    {"cyclesToSeconds", perfCyclesToSeconds,
     "Convert a rdtsc result to (double) seconds"},
    {"cyclesToNanos", perfCyclesToNanoseconds,
     "Convert a rdtsc result to (uint64_t) nanoseconds"},
    {"div32", div32,
     "32-bit integer division instruction"},
    {"div64", div64,
     "64-bit integer division instruction"},
    {"functionCall", functionCall,
     "Call a function that has not been inlined"},
    {"functionDereference", functionDereference,
     "Randomly dereference a function array of size 50"},
    {"mapCreate", mapCreate,
     "Create+delete entry in std::map"},
    {"mapLookup", mapLookup,
     "Lookup in std::map<uint64_t, uint64_t>"},
    {"memcpyCached100", memcpyCached100,
     "memcpy 100 bytes with hot/fixed dst and src"},
    {"memcpyCached1000", memcpyCached1000,
     "memcpy 1000 bytes with hot/fixed dst and src"},
    {"memcpyCachedDst100", memcpyCachedDst100,
     "memcpy 100 bytes with hot/fixed dst and cold src"},
    {"memcpyCachedDst1000", memcpyCachedDst1000,
     "memcpy 1000 bytes with hot/fixed dst and cold src"},
    {"memcpyCold100", memcpyCold100,
     "memcpy 100 bytes with cold dst and src"},
    {"memcpyCold1000", memcpyCold1000,
     "memcpy 1000 bytes with cold dst and src"},
    {"rdtsc", rdtscTest,
     "Read the fine-grain cycle counter"},
    {"serialize", serialize,
     "cpuid instruction for serialize"}
};

/**
 * Runs a particular test and prints a one-line result message.
 *
 * \param info
 *      Describes the test to run.
 */
void runTest(TestInfo& info)
{
    double secs = info.func();
    int width = printf("%-23s ", info.name);
    if (secs < 1.0e-06) {
        width += printf("%8.2fns", 1e09*secs);
    } else if (secs < 1.0e-03) {
        width += printf("%8.2fus", 1e06*secs);
    } else if (secs < 1.0) {
        width += printf("%8.2fms", 1e03*secs);
    } else {
        width += printf("%8.2fs", secs);
    }
    printf("%*s %s\n", 26-width, "", info.description);
}

int
main(int argc, char *argv[])
{
    bindThreadToCpu(3);
    if (argc == 1) {
        // No test names specified; run all tests.
        for (int i = 0; i < sizeof(tests)/sizeof(TestInfo); ++i) {
            runTest(tests[i]);
        }
    } else {
        // Run only the tests that were specified on the command line.
        for (int i = 1; i < argc; i++) {
            bool foundTest = false;
            for (int i = 0; i < sizeof(tests)/sizeof(TestInfo); ++i) {
                if (strcmp(argv[i], tests[i].name) == 0) {
                    foundTest = true;
                    runTest(tests[i]);
                    break;
                }
            }
            if (!foundTest) {
                int width = printf("%-18s ??", argv[i]);
                printf("%*s No such test\n", 26-width, "");
            }
        }
    }
}