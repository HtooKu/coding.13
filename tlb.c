/*
 * OSTEP Chapter 19: The Translation Lookaside Buffer (TLB)
 * Homework questions 1, 2, 5, 6, and 7.
 *
 * Question 1:
 * gettimeofday() reports time in seconds and microseconds, but the
 * unit used to report time is not the same as guaranteed accuracy.
 * Its effective resolution depends on the operating system and
 * hardware. A single memory access takes only nanoseconds, which is
 * too short to measure reliably with this timer. Repeat the page
 * accesses in a loop so the total timed interval is much longer than
 * the timer's resolution (this program targets about 20 milliseconds),
 * then divide elapsed time by the number of page accesses. The program
 * also estimates the smallest positive time change observed across
 * repeated gettimeofday() calls on this machine.
 *
 * Question 2:
 * This program accepts the number of pages and number of trials as
 * command-line arguments. It allocates that many pages, accesses one
 * byte at the beginning of each page (using the system page size as
 * the stride), and reports the average nanoseconds per page access
 * for each trial. Example: ./tlb 16 10
 *
 * Question 5:
 * The memory is accessed through a pointer to volatile unsigned char.
 * Volatile makes each read an observable access, preventing the
 * compiler from deleting the page-access loop as dead code. The
 * values read are accumulated and stored in a volatile sink after
 * each timed run so the work has an observable result.
 *
 * Question 6:
 * On Linux, pin the process to one allowed CPU with taskset, for
 * example: taskset -c 2 ./tlb 16 10. Check which CPUs are available
 * with: taskset -pc $$
 * The CPU number must be in the process's allowed CPU set. In C,
 * sched_setaffinity() can also set CPU affinity. If the process
 * migrates between CPUs, it may use a different CPU's TLB. That CPU
 * may not have the needed translations cached, causing extra TLB
 * misses and noisier, less consistent measurements.
 *
 * Question 7:
 * Yes. The first access to a page can cause a page fault and require
 * the operating system to allocate and zero a physical page
 * (demand-zeroing). This first-touch cost would distort the timing.
 * The program writes to one byte on every page before starting the
 * timed trials, so the pages are allocated and initialized in advance.
 */

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

static volatile unsigned long long result_sink = 0;

/* Return current wall-clock time in microseconds. */
static double get_time_us(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        exit(EXIT_FAILURE);
    }

    return (double)tv.tv_sec * 1000000.0 + (double)tv.tv_usec;
}

/* Estimate timer granularity from the smallest positive observed delta. */
static double estimate_timer_step_us(void)
{
    double previous = get_time_us();
    double smallest = 0.0;

    for (int i = 0; i < 10000; i++) {
        double current = get_time_us();
        double delta = current - previous;

        if (delta > 0.0 && (smallest == 0.0 || delta < smallest)) {
            smallest = delta;
        }
        previous = current;
    }

    return smallest;
}

/* Read the first byte of every page repeatedly. */
static unsigned long long access_pages(volatile unsigned char *array,
                                       int pages,
                                       long page_size,
                                       long repetitions)
{
    unsigned long long sum = 0;

    for (long r = 0; r < repetitions; r++) {
        for (int p = 0; p < pages; p++) {
            size_t offset = (size_t)p * (size_t)page_size;
            sum += array[offset];
        }
    }

    result_sink = sum;
    return sum;
}

int main(int argc, char *argv[])
{
    char *end = NULL;
    long pages_arg;
    long trials_arg;

    if (argc != 3) {
        fprintf(stderr, "Usage: ./tlb <pages> <trials>\\n");
        return EXIT_FAILURE;
    }

    errno = 0;
    pages_arg = strtol(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\\0' ||
        pages_arg < 1 || pages_arg > INT_MAX) {
        fprintf(stderr, "Pages must be a positive integer.\\n");
        return EXIT_FAILURE;
    }

    errno = 0;
    trials_arg = strtol(argv[2], &end, 10);
    if (errno != 0 || end == argv[2] || *end != '\\0' ||
        trials_arg < 1 || trials_arg > INT_MAX) {
        fprintf(stderr, "Trials must be a positive integer.\\n");
        return EXIT_FAILURE;
    }

    int pages = (int)pages_arg;
    int trials = (int)trials_arg;
    long page_size = sysconf(_SC_PAGESIZE);

    if (page_size <= 0) {
        fprintf(stderr, "Could not determine the system page size.\\n");
        return EXIT_FAILURE;
    }

    if ((size_t)pages > SIZE_MAX / (size_t)page_size) {
        fprintf(stderr, "Requested allocation is too large.\\n");
        return EXIT_FAILURE;
    }

    size_t allocation_size = (size_t)pages * (size_t)page_size;
    volatile unsigned char *array =
        (volatile unsigned char *)malloc(allocation_size);

    if (array == NULL) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    /* Touch every page before timing to avoid first-touch costs. */
    for (int p = 0; p < pages; p++) {
        array[(size_t)p * (size_t)page_size] = 1;
    }

    double timer_step = estimate_timer_step_us();
    printf("Pages: %d\\n", pages);
    printf("Page size: %ld bytes\\n", page_size);
    if (timer_step > 0.0) {
        printf("Smallest observed timer increment: %.3f microseconds\\n",
               timer_step);
    } else {
        printf("Timer increment could not be estimated.\\n");
    }

    /* Calibrate repetitions to make each trial last about 20 ms. */
    long calibration_repetitions = 100;
    double start = get_time_us();
    access_pages(array, pages, page_size, calibration_repetitions);
    double calibration_elapsed = get_time_us() - start;

    if (calibration_elapsed <= 0.0) {
        calibration_elapsed = 1.0;
    }

    double microseconds_per_pass =
        calibration_elapsed / (double)calibration_repetitions;
    long repetitions = (long)(20000.0 / microseconds_per_pass);

    if (repetitions < 1) {
        repetitions = 1;
    }
    if (repetitions > 10000000L) {
        repetitions = 10000000L;
    }

    printf("Repetitions per trial: %ld\\n", repetitions);
    printf("Trials: %d\\n", trials);

    for (int t = 0; t < trials; t++) {
        start = get_time_us();
        access_pages(array, pages, page_size, repetitions);
        double elapsed_us = get_time_us() - start;

        double total_accesses = (double)pages * (double)repetitions;
        double ns_per_access = (elapsed_us * 1000.0) / total_accesses;

        printf("Trial %d: %.2f ns per page access (%.2f ms total)\\n",
               t + 1, ns_per_access, elapsed_us / 1000.0);
    }

    printf("Checksum: %llu\\n", result_sink);
    free((void *)array);
    return EXIT_SUCCESS;
}
