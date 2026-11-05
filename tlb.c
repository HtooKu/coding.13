// 1) The gettimeofday() measures time in microseconds. Though 
// its precision depends on the OS and hardware.
//Individual memory access can only take a few nanoseconds,which is much
// shorter than the microsecond, so measuring a single page would be unreliable.
//To get a more accurate measurement, I repeat page access multiple times in a loop
//, measure elapse time total, and then divide by the toal number of access. This 
// reduces the impact of timer resolution and measurement overhead. 

// 5) The compiler may remove a loop is it determines that the loop does not 
//produce any results that are used elsewhere in program. 
// To preent this, the memory array is declared as volatile. This tells the compiler
// that memory access are obserable and shouldn't be removed. I also
//use a checksum to combine all the values read from the array and print it after ward. 
//This makes sure that the memory access are actually done. 

// 6) CPU pinning can be used to keep a program running on one CPU.
//    On Linux, the taskset command can restrict a program to a
//    specific CPU core. For example: taskset -c 0 ./tlb 16 10.

//    Without CPU pinning, the operating system scheduler may move
//    the program between different CPUs. Since each CPU can have
//    its own TLB, moving between CPUs can cause additional TLB
//    misses and inconsistent timing measurements.

// 7) Initialization can affect the time. When memory is access for the first
// time, the OS may need to allocate physical memory and intialize at 0.
// This is demand-zeroing and can cause issues. To stop it you can intialize one byte 
// every page before starting the timer. This makes sure tahat the page was already accessed and 
// the initial allocation costs are not included in timing experiment. 









#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return tv.tv_sec *1000000.0 + tv.tv_usec;
}


int main(int argc, char *argv[]){

    if(argc != 3){
        printf("Usage: ./tlb <pages> <trials>\n");
        return 1;
    }
    
    int pages = atoi(argv[1]);
    int trials = atoi(argv[2]);

    if (pages <= 0 || trials <= 0){
        printf("Pages and trials must be positive.\n");
        return 1;
    }
    int page_size = sysconf(_SC_PAGESIZE);
    size_t size = (size_t)pages * page_size;

    volatile unsigned char *array = malloc(size);

    if (array == NULL){
        printf(" Memory allocation failed. \n");
        return 1;
    }
    
    for (int i = 0; i < pages; i++) {
        array[(size_t)i * page_size] = 1;
    }

    // Choose enough repetitions to make each trial measurable.
    int repetitions = 10000;

    unsigned long long checksum = 0;

    printf("Pages: %d\n", pages);
    printf("Page size: %d bytes\n", page_size);
    printf("Trials: %d\n", trials);

    for (int t = 0; t < trials; t++) {

        double start = get_time();

        for (int r = 0; r < repetitions; r++) {
            for (int i = 0; i < pages; i++) {
                checksum += array[(size_t)i * page_size];
            }
        }

        double end = get_time();

        double total_accesses =
            (double)repetitions * pages;

        double average_time =
            (end - start) / total_accesses;

        printf("Trial %d: %.4f microseconds per page access\n",
               t + 1, average_time);
    }

    printf("Checksum: %llu\n", checksum);

    free((void *)array);

    return 0;
}
