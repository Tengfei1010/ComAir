#include "aproflib.h"
//#include "logger.h"

// page table

// L0  28-31
// L1  19-27
// L2  10-18
// L3   0-9

void **pL0 = NULL;
void **pL1 = NULL;
void **pL2 = NULL;

unsigned long *pL3 = NULL;

unsigned long prev = 0;
unsigned long *prev_pL3 = NULL;

// page table

char log_str[BUFFERSIZE];
unsigned long count = 0;
unsigned long sampling_count = 0;
struct stack_elem shadow_stack[STACK_SIZE];
int stack_top = -1;

// used to sampling
static int old_value = -1;


unsigned long aprof_query_page_table(unsigned long addr) {

    if ((addr & NEG_L3_MASK) == prev) {
        return prev_pL3[addr & L3_MASK];
    }


    unsigned long tmp = (addr & L0_MASK) >> 28;

    if (pL0[tmp] == NULL) {
        return 0;
    }

    pL1 = pL0[tmp];

    tmp = (addr & L1_MASK) >> 19;

    if (pL1[tmp] == NULL) {
        return 0;
    }

    pL2 = pL1[tmp];

    tmp = (addr & L2_MASK) >> 10;

    if (pL2[tmp] == NULL) {
        return 0;
    }

    pL3 = (unsigned long *) pL2[tmp];

    prev = addr & NEG_L3_MASK;
    prev_pL3 = pL3;

    return pL3[addr & L3_MASK];
}


void aprof_insert_page_table(unsigned long addr, unsigned long count) {

    if ((addr & NEG_L3_MASK) == prev) {
        prev_pL3[addr & L3_MASK] = count;
        return;
    }


    unsigned long tmp = (addr & L0_MASK) >> 28;

    if (pL0[tmp] == NULL) {
        pL0[tmp] = (void **) malloc(sizeof(void *) * L1_TABLE_SIZE);
        memset(pL0[tmp], 0, sizeof(void *) * L1_TABLE_SIZE);
    }

    pL1 = pL0[tmp];

    tmp = (addr & L1_MASK) >> 19;

    if (pL1[tmp] == NULL) {
        pL1[tmp] = (void **) malloc(sizeof(void *) * L1_TABLE_SIZE);
        memset(pL1[tmp], 0, sizeof(void *) * L1_TABLE_SIZE);
    }

    pL2 = pL1[tmp];

    tmp = (addr & L2_MASK) >> 10;

    if (pL2[tmp] == NULL) {
        pL2[tmp] = (unsigned long *) malloc(sizeof(unsigned long) * L3_TABLE_SIZE);
        memset(pL2[tmp], 0, sizeof(unsigned long) * L3_TABLE_SIZE);
    }

    pL3 = (unsigned long *) pL2[tmp];

    prev = addr & NEG_L3_MASK;
    prev_pL3 = pL3;

    pL3[addr & L3_MASK] = count;

}

void aprof_destroy_page_table() {
    int i, j, k;

    for (i = 0; i < L0_TABLE_SIZE; i++) {
        if (pL0[i] == NULL) {
            continue;
        }

        pL1 = (void **) pL0[i];

        for (j = 0; j < L1_TABLE_SIZE; j++) {
            if (pL1[j] == NULL) {
                continue;
            }

            pL2 = (void **) pL1[j];

            for (k = 0; k < L1_TABLE_SIZE; k++) {
                if (pL2[k] != NULL) {
                    free(pL2[k]);
                }
            }

            free(pL2);
        }

        free(pL1);
    }

    free(pL0);
}

char *aprof_init_share_mem() {
    int fd = shm_open(APROF_MEM_LOG, O_RDWR | O_CREAT | O_EXCL, 0777);

    if (fd < 0) {
        fd = shm_open(APROF_MEM_LOG, O_RDWR, 0777);

    } else
        ftruncate(fd, BUFFERSIZE);

    char *pcBuffer = (char *) mmap(0, BUFFERSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    return pcBuffer;
}

void aprof_init() {

    // init page table
    pL0 = (void **) malloc(sizeof(void *) * L0_TABLE_SIZE);
    memset(pL0, 0, sizeof(void *) * L0_TABLE_SIZE);
}

void aprof_write(void *memory_addr, unsigned int length) {

    unsigned long start_addr = (unsigned long) memory_addr;

    for (unsigned long i = start_addr; i < start_addr + length; i++) {
        aprof_insert_page_table(i, count);
    }

}

void aprof_read(void *memory_addr, unsigned int length) {
    unsigned long start_addr = (unsigned long) memory_addr;

    for (unsigned long i = start_addr; i < (start_addr + length); i++) {

        // We assume that w has been wrote before reading.
        // ts[w] > 0 and ts[w] < S[top]
        unsigned long ts_w = aprof_query_page_table(i);
        if (ts_w < shadow_stack[stack_top].ts) {

            shadow_stack[stack_top].rms++;

            if (ts_w != 0) {
                for (int j = stack_top; j > 0; j--) {

                    if (shadow_stack[j].ts <= ts_w) {
                        shadow_stack[j].rms--;
                        break;
                    }
                }
            }
        }

        aprof_insert_page_table(i, count);
    }

}

void aprof_increment_rms() {
    shadow_stack[stack_top].rms++;

}

void aprof_call_before(int funcId) {
    count++;
    stack_top++;
    shadow_stack[stack_top].funcId = funcId;
    shadow_stack[stack_top].ts = count;
    shadow_stack[stack_top].rms = 0;
    // newEle->cost update in aprof_return
    shadow_stack[stack_top].cost = 0;

}

void aprof_return(unsigned long numCost, unsigned long rms) {

    shadow_stack[stack_top].cost += numCost;
    shadow_stack[stack_top].rms += rms;

//    log_fatal(" ID %d ; RMS %ld ; Cost %ld ;",
//              shadow_stack[stack_top].funcId,
//              shadow_stack[stack_top].rms,
//              shadow_stack[stack_top].cost
//    );

//    char str[50];
//    sprintf(str, "ID %d , RMS %ld , Cost %ld \n",
//            shadow_stack[stack_top].funcId,
//            shadow_stack[stack_top].rms,
//            shadow_stack[stack_top].cost);
//    strcat(log_str, str);

    if (stack_top >= 1) {

        shadow_stack[stack_top - 1].rms += shadow_stack[stack_top].rms;
        shadow_stack[stack_top - 1].cost += shadow_stack[stack_top].cost;
        stack_top--;

    } else {
        // log result to memory.
//        void *ptr = aprof_init_share_mem();
//        strcpy((char *) ptr, log_str);
        aprof_destroy_page_table();
    }

}

//=========================================================================
//= Multiplicative LCG for generating uniform(0.0, 1.0) random numbers    =
//=   - x_n = 7^5*x_(n-1)mod(2^31 - 1)                                    =
//=   - With x seeded to 1 the 10000th x value should be 1043618065       =
//=   - From R. Jain, "The Art of Computer Systems Performance Analysis," =
//=     John Wiley & Sons, 1991. (Page 443, Figure 26.2)                  =
//=========================================================================
static double aprof_rand_val(int seed) {
    const long a = 16807;  // Multiplier
    const long m = 2147483647;  // Modulus
    const long q = 127773;  // m div a
    const long r = -2836;  // m mod a
    static long x;               // Random int value
    long x_div_q;         // x divided by q
    long x_mod_q;         // x modulo q
    long x_new;           // New x value

    // Set the seed if argument is non-zero and then return zero
    if (seed > 0) {
        x = seed;
        return (0.0);
    }

    // RNG using integer arithmetic
    x_div_q = x / q;
    x_mod_q = x % q;
    x_new = (a * x_mod_q) - (r * x_div_q);
    if (x_new > 0)
        x = x_new;
    else
        x = x_new + m;

    // Return a random value between 0.0 and 1.0
    return ((double) x / m);
}

//===========================================================================
//=  Function to generate geometrically distributed random variables        =
//=    - Input:  Probability of success p                                   =
//=    - Output: Returns with geometrically distributed random variable     =
//===========================================================================
int aprof_geo(int iRate) {
    double p = 1 / (double) iRate;
    double z;                     // Uniform random number (0 < z < 1)
    double geo_value;             // Computed geometric value to be returned

    do {
        // Pull a uniform random number (0 < z < 1)
        do {
            z = aprof_rand_val(0);
        } while ((z == 0) || (z == 1));

        // Compute geometric random variable using inversion method
        geo_value = (int) (log(z) / log(1.0 - p)) + 1;
    } while ((int) geo_value == old_value + 1);

    old_value = (int) geo_value;
    // log sampling call chain number
//    sampling_count = count - sampling_count;
//    log_fatal("sampling count: %ld;", sampling_count);
    return old_value;
}


void PrintExecutionCost(long numCost) {
    printf("%ld", numCost);
}