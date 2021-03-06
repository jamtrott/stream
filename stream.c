/*-----------------------------------------------------------------------*/
/* Program: STREAM                                                       */
/* Revision: $Id: stream.c,v 5.10 2013/01/17 16:01:06 mccalpin Exp mccalpin $ */
/* Original code developed by John D. McCalpin                           */
/* Programmers: John D. McCalpin                                         */
/*              Joe R. Zagar                                             */
/*                                                                       */
/* This program measures memory transfer rates in MB/s for simple        */
/* computational kernels coded in C.                                     */
/*-----------------------------------------------------------------------*/
/* Copyright 1991-2013: John D. McCalpin                                 */
/*-----------------------------------------------------------------------*/
/* License:                                                              */
/*  1. You are free to use this program and/or to redistribute           */
/*     this program.                                                     */
/*  2. You are free to modify this program for your own use,             */
/*     including commercial use, subject to the publication              */
/*     restrictions in item 3.                                           */
/*  3. You are free to publish results obtained from running this        */
/*     program, or from works that you derive from this program,         */
/*     with the following limitations:                                   */
/*     3a. In order to be referred to as "STREAM benchmark results",     */
/*         published results must be in conformance to the STREAM        */
/*         Run Rules, (briefly reviewed below) published at              */
/*         http://www.cs.virginia.edu/stream/ref.html                    */
/*         and incorporated herein by reference.                         */
/*         As the copyright holder, John McCalpin retains the            */
/*         right to determine conformity with the Run Rules.             */
/*     3b. Results based on modified source code or on runs not in       */
/*         accordance with the STREAM Run Rules must be clearly          */
/*         labelled whenever they are published.  Examples of            */
/*         proper labelling include:                                     */
/*           "tuned STREAM benchmark results"                            */
/*           "based on a variant of the STREAM benchmark code"           */
/*         Other comparable, clear, and reasonable labelling is          */
/*         acceptable.                                                   */
/*     3c. Submission of results to the STREAM benchmark web site        */
/*         is encouraged, but not required.                              */
/*  4. Use of this program or creation of derived works based on this    */
/*     program constitutes acceptance of these licensing restrictions.   */
/*  5. Absolutely no warranty is expressed or implied.                   */
/*-----------------------------------------------------------------------*/
# include <stdio.h>
# include <unistd.h>
# include <math.h>
# include <float.h>
# include <limits.h>
# include <sys/time.h>

#if defined(ENABLE_GATHER) || defined(ENABLE_SCATTER)
#include <stdlib.h>
#include <time.h>
#endif

/*-----------------------------------------------------------------------
 * INSTRUCTIONS:
 *
 *	1) STREAM requires different amounts of memory to run on different
 *           systems, depending on both the system cache size(s) and the
 *           granularity of the system timer.
 *     You should adjust the value of 'STREAM_ARRAY_SIZE' (below)
 *           to meet *both* of the following criteria:
 *       (a) Each array must be at least 4 times the size of the
 *           available cache memory. I don't worry about the difference
 *           between 10^6 and 2^20, so in practice the minimum array size
 *           is about 3.8 times the cache size.
 *           Example 1: One Xeon E3 with 8 MB L3 cache
 *               STREAM_ARRAY_SIZE should be >= 4 million, giving
 *               an array size of 30.5 MB and a total memory requirement
 *               of 91.5 MB.
 *           Example 2: Two Xeon E5's with 20 MB L3 cache each (using OpenMP)
 *               STREAM_ARRAY_SIZE should be >= 20 million, giving
 *               an array size of 153 MB and a total memory requirement
 *               of 458 MB.
 *       (b) The size should be large enough so that the 'timing calibration'
 *           output by the program is at least 20 clock-ticks.
 *           Example: most versions of Windows have a 10 millisecond timer
 *               granularity.  20 "ticks" at 10 ms/tic is 200 milliseconds.
 *               If the chip is capable of 10 GB/s, it moves 2 GB in 200 msec.
 *               This means the each array must be at least 1 GB, or 128M elements.
 *
 *      Version 5.10 increases the default array size from 2 million
 *          elements to 10 million elements in response to the increasing
 *          size of L3 caches.  The new default size is large enough for caches
 *          up to 20 MB.
 *      Version 5.10 changes the loop index variables from "register int"
 *          to "ssize_t", which allows array indices >2^32 (4 billion)
 *          on properly configured 64-bit systems.  Additional compiler options
 *          (such as "-mcmodel=medium") may be required for large memory runs.
 *
 *      Array size can be set at compile time without modifying the source
 *          code for the (many) compilers that support preprocessor definitions
 *          on the compile line.  E.g.,
 *                gcc -O -DSTREAM_ARRAY_SIZE=100000000 stream.c -o stream.100M
 *          will override the default size of 10M with a new size of 100M elements
 *          per array.
 */
#ifndef STREAM_ARRAY_SIZE
#   define STREAM_ARRAY_SIZE	10000000
#endif

#ifndef STREAM_INDEX_ARRAY_SIZE
#   define STREAM_INDEX_ARRAY_SIZE	10000000
#endif

/*  2) STREAM runs each kernel "NTIMES" times and reports the *best* result
 *         for any iteration after the first, therefore the minimum value
 *         for NTIMES is 2.
 *      There are no rules on maximum allowable values for NTIMES, but
 *         values larger than the default are unlikely to noticeably
 *         increase the reported performance.
 *      NTIMES can also be set on the compile line without changing the source
 *         code using, for example, "-DNTIMES=7".
 */
#ifdef NTIMES
#if NTIMES<=1
#   define NTIMES	10
#endif
#endif
#ifndef NTIMES
#   define NTIMES	10
#endif

/*  Users are allowed to modify the "OFFSET" variable, which *may* change the
 *         relative alignment of the arrays (though compilers may change the
 *         effective offset by making the arrays non-contiguous on some systems).
 *      Use of non-zero values for OFFSET can be especially helpful if the
 *         STREAM_ARRAY_SIZE is set to a value close to a large power of 2.
 *      OFFSET can also be set on the compile line without changing the source
 *         code using, for example, "-DOFFSET=56".
 */
#ifndef OFFSET
#   define OFFSET	0
#endif

/*
 *	3) Compile the code with optimization.  Many compilers generate
 *       unreasonably bad code before the optimizer tightens things up.
 *     If the results are unreasonably good, on the other hand, the
 *       optimizer might be too smart for me!
 *
 *     For a simple single-core version, try compiling with:
 *            cc -O stream.c -o stream
 *     This is known to work on many, many systems....
 *
 *     To use multiple cores, you need to tell the compiler to obey the OpenMP
 *       directives in the code.  This varies by compiler, but a common example is
 *            gcc -O -fopenmp stream.c -o stream_omp
 *       The environment variable OMP_NUM_THREADS allows runtime control of the
 *         number of threads/cores used when the resulting "stream_omp" program
 *         is executed.
 *
 *     To run with single-precision variables and arithmetic, simply add
 *         -DSTREAM_TYPE=float
 *     to the compile line.
 *     Note that this changes the minimum array sizes required --- see (1) above.
 *
 *     The preprocessor directive "TUNED" does not do much -- it simply causes the
 *       code to call separate functions to execute each kernel.  Trivial versions
 *       of these functions are provided, but they are *not* tuned -- they just
 *       provide predefined interfaces to be replaced with tuned code.
 *
 *
 *	4) Optional: Mail the results to mccalpin@cs.virginia.edu
 *         Be sure to include info that will help me understand:
 *		a) the computer hardware configuration (e.g., processor model, memory type)
 *		b) the compiler name/version and compilation flags
 *      c) any run-time information (such as OMP_NUM_THREADS)
 *		d) all of the output from the test case.
 *
 * Thanks!
 *
 *-----------------------------------------------------------------------*/

# define HLINE "-------------------------------------------------------------\n"

# ifndef MIN
# define MIN(x,y) ((x)<(y)?(x):(y))
# endif
# ifndef MAX
# define MAX(x,y) ((x)>(y)?(x):(y))
# endif

#ifndef STREAM_TYPE
#define STREAM_TYPE double
#endif

#ifndef INDEX_TYPE
#define INDEX_TYPE int
#endif

#if defined(ENABLE_GATHER)
#define NUM_KERNELS_GATHER 1
#else
#define NUM_KERNELS_GATHER 0
#endif

#if defined(ENABLE_SCATTER)
#define NUM_KERNELS_SCATTER 1
#else
#define NUM_KERNELS_SCATTER 0
#endif

#if defined(ENABLE_INDIRECT_DOT_PRODUCT)
#define NUM_KERNELS_INDIRECT_DOT_PRODUCT 1
#else
#define NUM_KERNELS_INDIRECT_DOT_PRODUCT 0
#endif

#define NUM_KERNELS                             \
    (4 +                                        \
     NUM_KERNELS_GATHER +                       \
     NUM_KERNELS_SCATTER +                      \
     NUM_KERNELS_INDIRECT_DOT_PRODUCT)

static STREAM_TYPE	a[STREAM_ARRAY_SIZE+OFFSET],
    b[STREAM_ARRAY_SIZE+OFFSET],
    c[STREAM_ARRAY_SIZE+OFFSET];

#if defined(ENABLE_GATHER) || defined(ENABLE_SCATTER) || defined(ENABLE_INDIRECT_DOT_PRODUCT)
static STREAM_TYPE	d[STREAM_INDEX_ARRAY_SIZE+OFFSET];
static INDEX_TYPE       i[STREAM_INDEX_ARRAY_SIZE+OFFSET];
#endif
#if defined(ENABLE_SCATTER)
static STREAM_TYPE      e[STREAM_ARRAY_SIZE+OFFSET];
#endif
#if defined(ENABLE_INDIRECT_DOT_PRODUCT)
static STREAM_TYPE x;
#endif

static double	avgtime[NUM_KERNELS] = {0}, maxtime[NUM_KERNELS] = {0},
    mintime[NUM_KERNELS] = {FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX};

static char	*label[NUM_KERNELS] = {
    "Copy:      ", "Scale:     ",
    "Add:       ", "Triad:     ",
#ifdef ENABLE_GATHER
    "Gather:    ",
#endif
#ifdef ENABLE_SCATTER
    "Scatter:   ",
#endif
#ifdef ENABLE_INDIRECT_DOT_PRODUCT
    "Ind.dot:   ",
#endif
};

static double	bytes[NUM_KERNELS] = {
    2 * sizeof(STREAM_TYPE) * STREAM_ARRAY_SIZE,
    2 * sizeof(STREAM_TYPE) * STREAM_ARRAY_SIZE,
    3 * sizeof(STREAM_TYPE) * STREAM_ARRAY_SIZE,
    3 * sizeof(STREAM_TYPE) * STREAM_ARRAY_SIZE,
#ifdef ENABLE_GATHER
    sizeof(STREAM_TYPE) * MIN(STREAM_ARRAY_SIZE, STREAM_INDEX_ARRAY_SIZE) +
    sizeof(STREAM_TYPE) * STREAM_INDEX_ARRAY_SIZE +
    sizeof(INDEX_TYPE) * STREAM_INDEX_ARRAY_SIZE,
#endif
#ifdef ENABLE_SCATTER
    sizeof(STREAM_TYPE) * MIN(STREAM_ARRAY_SIZE, STREAM_INDEX_ARRAY_SIZE) +
    sizeof(STREAM_TYPE) * STREAM_INDEX_ARRAY_SIZE +
    sizeof(INDEX_TYPE) * STREAM_INDEX_ARRAY_SIZE,
#endif
#ifdef ENABLE_INDIRECT_DOT_PRODUCT
    sizeof(STREAM_TYPE) * MIN(STREAM_ARRAY_SIZE, STREAM_INDEX_ARRAY_SIZE) +
    sizeof(STREAM_TYPE) * STREAM_INDEX_ARRAY_SIZE +
    sizeof(INDEX_TYPE) * STREAM_INDEX_ARRAY_SIZE,
#endif
};

extern double mysecond();
extern void checkSTREAMresults();
#ifdef TUNED
extern void tuned_STREAM_Copy();
extern void tuned_STREAM_Scale(STREAM_TYPE scalar);
extern void tuned_STREAM_Add();
extern void tuned_STREAM_Triad(STREAM_TYPE scalar);
#endif
#ifdef _OPENMP
extern int omp_get_num_threads();
#endif
int
main()
{
    int			quantum, checktick();
    int			BytesPerWord;
#if defined(ENABLE_GATHER) || defined(ENABLE_SCATTER) || defined(ENABLE_INDIRECT_DOT_PRODUCT)
    int			BytesPerIndexWord;
    unsigned int	seed;
#endif
#if NUM_KERNELS > 4
    int			l;
#endif
    int			k;
    ssize_t		j;
    STREAM_TYPE		scalar;
    double		t, times[NUM_KERNELS][NTIMES];

    /* --- SETUP --- determine precision and check timing --- */

    printf(HLINE);
    printf("STREAM version $Revision: 5.10 $\n");
    printf(HLINE);
    BytesPerWord = sizeof(STREAM_TYPE);
    printf("This system uses %d bytes per array element.\n",
           BytesPerWord);
#if defined(ENABLE_GATHER) || defined(ENABLE_SCATTER) || defined(ENABLE_INDIRECT_DOT_PRODUCT)
    BytesPerIndexWord = sizeof(INDEX_TYPE);
    printf("Also, this system uses %d bytes per array index.\n",
           BytesPerIndexWord);
#endif

#if NUM_KERNELS > 4
    for (j=0; j<NUM_KERNELS; j++) {
        avgtime[j] = 0.0;
        mintime[j] = FLT_MAX;
        maxtime[j] = 0.0;
    }
#endif

    printf(HLINE);
#ifdef N
    printf("*****  WARNING: ******\n");
    printf("      It appears that you set the preprocessor variable N when compiling this code.\n");
    printf("      This version of the code uses the preprocesor variable STREAM_ARRAY_SIZE to control the array size\n");
    printf("      Reverting to default value of STREAM_ARRAY_SIZE=%llu\n",(unsigned long long) STREAM_ARRAY_SIZE);
    printf("*****  WARNING: ******\n");
#endif

    printf("Array size = %llu (elements), Offset = %d (elements)\n" , (unsigned long long) STREAM_ARRAY_SIZE, OFFSET);
    printf("Memory per array = %.1f MiB (= %.1f GiB).\n",
           BytesPerWord * ( (double) STREAM_ARRAY_SIZE / 1024.0/1024.0),
           BytesPerWord * ( (double) STREAM_ARRAY_SIZE / 1024.0/1024.0/1024.0));
#if defined(ENABLE_GATHER) || defined(ENABLE_SCATTER) || defined(ENABLE_INDIRECT_DOT_PRODUCT)
    printf("Index array size = %llu (elements), Offset = %d (elements)\n" , (unsigned long long) STREAM_INDEX_ARRAY_SIZE, OFFSET);
    printf("Memory per indexed array = %.1f MiB (= %.1f GiB).\n",
           BytesPerWord * ( (double) STREAM_INDEX_ARRAY_SIZE / 1024.0/1024.0),
           BytesPerWord * ( (double) STREAM_INDEX_ARRAY_SIZE / 1024.0/1024.0/1024.0));
    printf("Memory per index array = %.1f MiB (= %.1f GiB).\n",
           BytesPerIndexWord * ( (double) STREAM_INDEX_ARRAY_SIZE / 1024.0/1024.0),
           BytesPerIndexWord * ( (double) STREAM_INDEX_ARRAY_SIZE / 1024.0/1024.0/1024.0));
#ifdef ENABLE_SCATTER
    printf("Total memory required = %.1f MiB (= %.1f GiB).\n",
           (4.0 * BytesPerWord) * ( (double) STREAM_ARRAY_SIZE / 1024.0/1024.) +
           (1.0 * BytesPerWord) * ( (double) STREAM_INDEX_ARRAY_SIZE / 1024.0/1024.) +
           (1.0 * BytesPerIndexWord) * ( (double) STREAM_INDEX_ARRAY_SIZE / 1024.0/1024.),
           (4.0 * BytesPerWord) * ( (double) STREAM_ARRAY_SIZE / 1024.0/1024./1024.) +
           (1.0 * BytesPerWord) * ( (double) STREAM_INDEX_ARRAY_SIZE / 1024.0/1024./1024.) +
           (1.0 * BytesPerIndexWord) * ( (double) STREAM_INDEX_ARRAY_SIZE / 1024.0/1024./1024.));
#else
    printf("Total memory required = %.1f MiB (= %.1f GiB).\n",
           (3.0 * BytesPerWord) * ( (double) STREAM_ARRAY_SIZE / 1024.0/1024.) +
           (1.0 * BytesPerWord) * ( (double) STREAM_INDEX_ARRAY_SIZE / 1024.0/1024.) +
           (1.0 * BytesPerIndexWord) * ( (double) STREAM_INDEX_ARRAY_SIZE / 1024.0/1024.),
           (3.0 * BytesPerWord) * ( (double) STREAM_ARRAY_SIZE / 1024.0/1024./1024.) +
           (1.0 * BytesPerWord) * ( (double) STREAM_INDEX_ARRAY_SIZE / 1024.0/1024./1024.) +
           (1.0 * BytesPerIndexWord) * ( (double) STREAM_INDEX_ARRAY_SIZE / 1024.0/1024./1024.));
#endif
#else
    printf("Total memory required = %.1f MiB (= %.1f GiB).\n",
           (3.0 * BytesPerWord) * ( (double) STREAM_ARRAY_SIZE / 1024.0/1024.),
           (3.0 * BytesPerWord) * ( (double) STREAM_ARRAY_SIZE / 1024.0/1024./1024.));
#endif
    printf("Each kernel will be executed %d times.\n", NTIMES);
    printf(" The *best* time for each kernel (excluding the first iteration)\n");
    printf(" will be used to compute the reported bandwidth.\n");

#ifdef _OPENMP
    printf(HLINE);
#pragma omp parallel
    {
#pragma omp master
        {
            k = omp_get_num_threads();
            printf ("Number of Threads requested = %i\n",k);
        }
    }
#endif

#ifdef _OPENMP
    k = 0;
#pragma omp parallel
#pragma omp atomic
    k++;
    printf ("Number of Threads counted = %i\n",k);
#endif

    /* Get initial value for system clock. */
#pragma omp parallel for
    for (j=0; j<STREAM_ARRAY_SIZE; j++) {
        a[j] = 1.0;
        b[j] = 2.0;
        c[j] = 0.0;
    }

#if defined(ENABLE_GATHER) || defined(ENABLE_SCATTER) || defined(ENABLE_INDIRECT_DOT_PRODUCT)
#pragma omp parallel for
    for (j=0; j<STREAM_INDEX_ARRAY_SIZE; j++) {
        d[j] = 1.0;
    }
#pragma omp parallel for
    for (j=0; j<STREAM_INDEX_ARRAY_SIZE; j++) {
        i[j] = j % STREAM_ARRAY_SIZE;
    }

#ifdef PERMUTE_INDEX_ARRAY
    /* Use the Fisher-Yates Shuffle algorithm
     * to generate an unbiased random permutation
     * for the irregular indices. */
#ifdef SRAND_SEED
    seed = SRAND_SEED;
#else
    seed = time(0);
#endif
    srand(seed);
    printf("The index array is randomly permuted (seed = %d)\n ",
           seed);
    for (j=0; j<STREAM_INDEX_ARRAY_SIZE-2; j++) {
        int k = j + rand() % (STREAM_INDEX_ARRAY_SIZE - j);
        INDEX_TYPE tmp = i[j];
        i[j] = i[k];
        i[k] = tmp;
    }
#endif
#endif
#ifdef ENABLE_SCATTER
#pragma omp parallel for
    for (j=0; j<STREAM_ARRAY_SIZE; j++) {
        e[j] = 0.0;
    }
#endif

    printf(HLINE);

    if  ( (quantum = checktick()) >= 1)
        printf("Your clock granularity/precision appears to be "
               "%d microseconds.\n", quantum);
    else {
        printf("Your clock granularity appears to be "
               "less than one microsecond.\n");
        quantum = 1;
    }

    t = mysecond();
#pragma omp parallel for
    for (j = 0; j < STREAM_ARRAY_SIZE; j++)
        a[j] = 2.0E0 * a[j];
    t = 1.0E6 * (mysecond() - t);

    printf("Each test below will take on the order"
           " of %d microseconds.\n", (int) t  );
    printf("   (= %d clock ticks)\n", (int) (t/quantum) );
    printf("Increase the size of the arrays if this shows that\n");
    printf("you are not getting at least 20 clock ticks per test.\n");

    printf(HLINE);

    printf("WARNING -- The above is only a rough guideline.\n");
    printf("For best results, please be sure you know the\n");
    printf("precision of your system timer.\n");
    printf(HLINE);

    /*	--- MAIN LOOP --- repeat test cases NTIMES times --- */

    scalar = 3.0;
    for (k=0; k<NTIMES; k++)
    {
        times[0][k] = mysecond();
#ifdef TUNED
        tuned_STREAM_Copy();
#else
#pragma omp parallel for
        for (j=0; j<STREAM_ARRAY_SIZE; j++)
            c[j] = a[j];
#endif
        times[0][k] = mysecond() - times[0][k];

        times[1][k] = mysecond();
#ifdef TUNED
        tuned_STREAM_Scale(scalar);
#else
#pragma omp parallel for
        for (j=0; j<STREAM_ARRAY_SIZE; j++)
            b[j] = scalar*c[j];
#endif
        times[1][k] = mysecond() - times[1][k];

        times[2][k] = mysecond();
#ifdef TUNED
        tuned_STREAM_Add();
#else
#pragma omp parallel for
        for (j=0; j<STREAM_ARRAY_SIZE; j++)
            c[j] = a[j]+b[j];
#endif
        times[2][k] = mysecond() - times[2][k];

        times[3][k] = mysecond();
#ifdef TUNED
        tuned_STREAM_Triad(scalar);
#else
#pragma omp parallel for
        for (j=0; j<STREAM_ARRAY_SIZE; j++)
            a[j] = b[j]+scalar*c[j];
#endif
        times[3][k] = mysecond() - times[3][k];

#if NUM_KERNELS > 4
        l = 4;
#endif
#ifdef ENABLE_GATHER
        times[l][k] = mysecond();
#pragma omp parallel for
        for (j=0; j<STREAM_INDEX_ARRAY_SIZE; j++)
            d[j] = a[i[j]];
        times[l][k] = mysecond() - times[l][k];
        l++;
#endif
#ifdef ENABLE_SCATTER
        times[l][k] = mysecond();
#pragma omp parallel for
        for (j=0; j<STREAM_INDEX_ARRAY_SIZE; j++)
            e[i[j]] = d[j];
        times[l][k] = mysecond() - times[l][k];
        l++;
#endif
#ifdef ENABLE_INDIRECT_DOT_PRODUCT
        x = 0.0;
        times[l][k] = mysecond();
#pragma omp parallel for
        for (j=0; j<STREAM_INDEX_ARRAY_SIZE; j++)
            x += d[j] * b[i[j]];
        times[l][k] = mysecond() - times[l][k];
        l++;
#endif
    }

    /*	--- SUMMARY --- */

    for (k=1; k<NTIMES; k++) /* note -- skip first iteration */
    {
        for (j=0; j<NUM_KERNELS; j++)
        {
            avgtime[j] = avgtime[j] + times[j][k];
            mintime[j] = MIN(mintime[j], times[j][k]);
            maxtime[j] = MAX(maxtime[j], times[j][k]);
        }
    }

    printf("Function    Best Rate MB/s  Avg time     Min time     Max time\n");
    for (j=0; j<NUM_KERNELS; j++) {
        avgtime[j] = avgtime[j]/(double)(NTIMES-1);

        printf("%s%12.1f  %11.6f  %11.6f  %11.6f\n", label[j],
               1.0E-06 * bytes[j]/mintime[j],
               avgtime[j],
               mintime[j],
               maxtime[j]);
    }
    printf(HLINE);

    /* --- Check Results --- */
    checkSTREAMresults();
    printf(HLINE);

    return 0;
}

# define	M	20

int
checktick()
{
    int		i, minDelta, Delta;
    double	t1, t2, timesfound[M];

/*  Collect a sequence of M unique time values from the system. */

    for (i = 0; i < M; i++) {
        t1 = mysecond();
        while( ((t2=mysecond()) - t1) < 1.0E-6 )
            ;
        timesfound[i] = t1 = t2;
    }

/*
 * Determine the minimum difference between these M values.
 * This result will be our estimate (in microseconds) for the
 * clock granularity.
 */

    minDelta = 1000000;
    for (i = 1; i < M; i++) {
        Delta = (int)( 1.0E6 * (timesfound[i]-timesfound[i-1]));
        minDelta = MIN(minDelta, MAX(Delta,0));
    }

    return(minDelta);
}



#ifndef USE_CLOCK_GETTIME
/* A gettimeofday routine to give access to the wall
   clock timer on most UNIX-like systems.  */

#include <sys/time.h>

double mysecond()
{
    struct timeval tp;
    struct timezone tzp;
    int i;

    i = gettimeofday(&tp,&tzp);
    return ( (double) tp.tv_sec + (double) tp.tv_usec * 1.e-6 );
}
#else
#include <time.h>
double mysecond()
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return ( (double) t.tv_sec + (double) t.tv_nsec * 1.e-9 );
}
#endif

#ifndef abs
#define abs(a) ((a) >= 0 ? (a) : -(a))
#endif
void checkSTREAMresults ()
{
    STREAM_TYPE aj,bj,cj,scalar;
    STREAM_TYPE aSumErr,bSumErr,cSumErr;
    STREAM_TYPE aAvgErr,bAvgErr,cAvgErr;
#ifdef ENABLE_GATHER
    STREAM_TYPE dj,dSumErr,dAvgErr;
#endif
#ifdef ENABLE_SCATTER
    STREAM_TYPE ej,eSumErr,eAvgErr;
#endif
#ifdef ENABLE_INDIRECT_DOT_PRODUCT
    STREAM_TYPE xj,xErr;
#endif
    double epsilon;
    ssize_t	j;
    int	k,ierr,err;

    /* reproduce initialization */
    aj = 1.0;
    bj = 2.0;
    cj = 0.0;
    /* a[] is modified during timing check */
    aj = 2.0E0 * aj;
    /* now execute timing loop */
    scalar = 3.0;
    for (k=0; k<NTIMES; k++)
    {
        cj = aj;
        bj = scalar*cj;
        cj = aj+bj;
        aj = bj+scalar*cj;
#ifdef ENABLE_GATHER
        dj = aj;
#endif
#ifdef ENABLE_SCATTER
#ifdef ENABLE_GATHER
        ej = aj;
#else
        ej = 0.0;
#endif
#endif
    }

    /* accumulate deltas between observed and expected results */
    aSumErr = 0.0;
    bSumErr = 0.0;
    cSumErr = 0.0;
    for (j=0; j<STREAM_ARRAY_SIZE; j++) {
        aSumErr += abs(a[j] - aj);
        bSumErr += abs(b[j] - bj);
        cSumErr += abs(c[j] - cj);
        // if (j == 417) printf("Index 417: c[j]: %f, cj: %f\n",c[j],cj);	// MCCALPIN
    }
    aAvgErr = aSumErr / (STREAM_TYPE) STREAM_ARRAY_SIZE;
    bAvgErr = bSumErr / (STREAM_TYPE) STREAM_ARRAY_SIZE;
    cAvgErr = cSumErr / (STREAM_TYPE) STREAM_ARRAY_SIZE;

#ifdef ENABLE_GATHER
    dSumErr = 0.0;
    for (j=0; j<STREAM_INDEX_ARRAY_SIZE; j++) {
        dSumErr += abs(d[j] - dj);
    }
    dAvgErr = dSumErr / (STREAM_TYPE) STREAM_INDEX_ARRAY_SIZE;
#endif
#ifdef ENABLE_SCATTER
    eSumErr = 0.0;
    for (j=0; j<STREAM_INDEX_ARRAY_SIZE; j++) {
        eSumErr += abs(e[i[j]] - ej);
    }
    eAvgErr = eSumErr / (STREAM_TYPE) STREAM_ARRAY_SIZE;
#endif
#ifdef ENABLE_INDIRECT_DOT_PRODUCT
    xj = 0.0;
    for (j=0; j<STREAM_INDEX_ARRAY_SIZE; j++)
        xj += d[j] * b[i[j]];
    xErr = x - xj;
#endif

    if (sizeof(STREAM_TYPE) == 4) {
        epsilon = 1.e-6;
    }
    else if (sizeof(STREAM_TYPE) == 8) {
        epsilon = 1.e-13;
    }
    else {
        printf("WEIRD: sizeof(STREAM_TYPE) = %lu\n",sizeof(STREAM_TYPE));
        epsilon = 1.e-6;
    }

    err = 0;
    if (abs(aAvgErr/aj) > epsilon) {
        err++;
        printf ("Failed Validation on array a[], AvgRelAbsErr > epsilon (%e)\n",epsilon);
        printf ("     Expected Value: %e, AvgAbsErr: %e, AvgRelAbsErr: %e\n",aj,aAvgErr,abs(aAvgErr)/aj);
        ierr = 0;
        for (j=0; j<STREAM_ARRAY_SIZE; j++) {
            if (abs(a[j]/aj-1.0) > epsilon) {
                ierr++;
#ifdef VERBOSE
                if (ierr < 10) {
                    printf("         array a: index: %ld, expected: %e, observed: %e, relative error: %e\n",
                           j,aj,a[j],abs((aj-a[j])/aAvgErr));
                }
#endif
            }
        }
        printf("     For array a[], %d errors were found.\n",ierr);
    }
    if (abs(bAvgErr/bj) > epsilon) {
        err++;
        printf ("Failed Validation on array b[], AvgRelAbsErr > epsilon (%e)\n",epsilon);
        printf ("     Expected Value: %e, AvgAbsErr: %e, AvgRelAbsErr: %e\n",bj,bAvgErr,abs(bAvgErr)/bj);
        printf ("     AvgRelAbsErr > Epsilon (%e)\n",epsilon);
        ierr = 0;
        for (j=0; j<STREAM_ARRAY_SIZE; j++) {
            if (abs(b[j]/bj-1.0) > epsilon) {
                ierr++;
#ifdef VERBOSE
                if (ierr < 10) {
                    printf("         array b: index: %ld, expected: %e, observed: %e, relative error: %e\n",
                           j,bj,b[j],abs((bj-b[j])/bAvgErr));
                }
#endif
            }
        }
        printf("     For array b[], %d errors were found.\n",ierr);
    }
    if (abs(cAvgErr/cj) > epsilon) {
        err++;
        printf ("Failed Validation on array c[], AvgRelAbsErr > epsilon (%e)\n",epsilon);
        printf ("     Expected Value: %e, AvgAbsErr: %e, AvgRelAbsErr: %e\n",cj,cAvgErr,abs(cAvgErr)/cj);
        printf ("     AvgRelAbsErr > Epsilon (%e)\n",epsilon);
        ierr = 0;
        for (j=0; j<STREAM_ARRAY_SIZE; j++) {
            if (abs(c[j]/cj-1.0) > epsilon) {
                ierr++;
#ifdef VERBOSE
                if (ierr < 10) {
                    printf("         array c: index: %ld, expected: %e, observed: %e, relative error: %e\n",
                           j,cj,c[j],abs((cj-c[j])/cAvgErr));
                }
#endif
            }
        }
        printf("     For array c[], %d errors were found.\n",ierr);
    }
#ifdef ENABLE_GATHER
    if (abs(dAvgErr/dj) > epsilon) {
        err++;
        printf ("Failed Validation on array d[], AvgRelAbsErr > epsilon (%e)\n",epsilon);
        printf ("     Expected Value: %e, AvgAbsErr: %e, AvgRelAbsErr: %e\n",dj,dAvgErr,abs(dAvgErr)/dj);
        printf ("     AvgRelAbsErr > Epsilon (%e)\n",epsilon);
        ierr = 0;
        for (j=0; j<STREAM_INDEX_ARRAY_SIZE; j++) {
            if (abs(d[j]/dj-1.0) > epsilon) {
                ierr++;
#ifdef VERBOSE
                if (ierr < 10) {
                    printf("         array d: index: %ld, expected: %e, observed: %e, relative error: %e\n",
                           j,dj,d[j],abs((dj-d[j])/dAvgErr));
                }
#endif
            }
        }
        printf("     For array d[], %d errors were found.\n",ierr);
    }
#endif
#ifdef ENABLE_SCATTER
    if (abs(eAvgErr/ej) > epsilon) {
        err++;
        printf ("Failed Validation on array e[], AvgRelAbsErr > epsilon (%e)\n",epsilon);
        printf ("     Expected Value: %e, AvgAbsErr: %e, AvgRelAbsErr: %e\n",ej,eAvgErr,abs(eAvgErr)/ej);
        printf ("     AvgRelAbsErr > Epsilon (%e)\n",epsilon);
        ierr = 0;
        for (j=0; j<STREAM_ARRAY_SIZE; j++) {
            if (abs(e[j]/ej-1.0) > epsilon) {
                ierr++;
#ifdef VERBOSE
                if (ierr < 10) {
                    printf("         array e: index: %ld, expected: %e, observed: %e, relative error: %e\n",
                           j,ej,e[j],abs((ej-e[j])/eAvgErr));
                }
#endif
            }
        }
        printf("     For array e[], %d errors were found.\n",ierr);
    }
#endif
#ifdef ENABLE_INDIRECT_DOT_PRODUCT
    if (abs(xErr/xj) > epsilon) {
        err++;
        printf ("Failed Validation on value x, AvgRelAbsErr > epsilon (%e)\n",epsilon);
        printf ("     Expected Value: %e, AvgAbsErr: %e, AvgRelAbsErr: %e\n",xj,xErr,abs(xErr)/xj);
        printf ("     AvgRelAbsErr > Epsilon (%e)\n",epsilon);
    }
#endif
    if (err == 0) {
        printf ("Solution Validates: avg error less than %e on all three arrays\n",epsilon);
    }
#ifdef VERBOSE
    printf ("Results Validation Verbose Results: \n");
    printf ("    Expected a(1), b(1), c(1): %f %f %f \n",aj,bj,cj);
    printf ("    Observed a(1), b(1), c(1): %f %f %f \n",a[1],b[1],c[1]);
    printf ("    Rel Errors on a, b, c:     %e %e %e \n",abs(aAvgErr/aj),abs(bAvgErr/bj),abs(cAvgErr/cj));
#endif
}

#ifdef TUNED
/* stubs for "tuned" versions of the kernels */
void tuned_STREAM_Copy()
{
    ssize_t j;
#pragma omp parallel for
    for (j=0; j<STREAM_ARRAY_SIZE; j++)
        c[j] = a[j];
}

void tuned_STREAM_Scale(STREAM_TYPE scalar)
{
    ssize_t j;
#pragma omp parallel for
    for (j=0; j<STREAM_ARRAY_SIZE; j++)
        b[j] = scalar*c[j];
}

void tuned_STREAM_Add()
{
    ssize_t j;
#pragma omp parallel for
    for (j=0; j<STREAM_ARRAY_SIZE; j++)
        c[j] = a[j]+b[j];
}

void tuned_STREAM_Triad(STREAM_TYPE scalar)
{
    ssize_t j;
#pragma omp parallel for
    for (j=0; j<STREAM_ARRAY_SIZE; j++)
        a[j] = b[j]+scalar*c[j];
}
/* end of stubs for the "tuned" versions of the kernels */
#endif
