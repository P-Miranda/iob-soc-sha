#if 0

extern "C"{
#include "system.h"
#include "periphs.h"
#include "iob-uart.h"

#include "iob-timer.h"

int printf_(const char* format, ...);
}

#define printf printf_

int main(int argc,const char* argv[])
{
    //init uart
    uart_init(UART_BASE,FREQ/BAUD);
    timer_init(TIMER_BASE);

    printf("Init base modules\n");

    printf("Init Versat\n");

    uart_finish();

    return 0;
}

#else

#include <cstdio>

#include "versat.hpp"
#include "utils.hpp"

#include "verilogWrapper.inc"

extern "C"{
#include "system.h"
#include "periphs.h"
#include "iob-uart.h"
#include "string.h"

#include "iob-timer.h"

#include "crypto/sha2.h"
#include "crypto/aes.h"

#include "../test_vectors.h"

int printf_(const char* format, ...);
}

#ifdef PC
#define uart_finish(...) ((void)0)
#define uart_init(...) ((void)0)
#else
#define printf printf_
#endif

#define MEMSET(base, location, value) (*((volatile int*) (base + (sizeof(int)) * location)) = value)
#define MEMGET(base, location)        (*((volatile int*) (base + (sizeof(int)) * location)))

// Automatically times a block in number of counts
struct TimeIt{
    int line;
    char fileId;

    TimeIt(int line,char fileId);
    ~TimeIt();
};

TimeIt::TimeIt(int line,char fileId){
    this->line = line;
    this->fileId = fileId;
    timer_reset();
}

TimeIt::~TimeIt(){
    unsigned long long end = timer_get_count();
    printf("%c:%d %llu\n",fileId,line,end);
}

#if 1
#define TIME_IT(ID) TimeIt timer_##__LINE__(__LINE__,ID)
#else
#define TIME_IT(ID) do{}while(0)
#endif

#define HASH_SIZE (256/8)

int ClearCache(){
#ifdef PC
    static int buffer[4096 * 4096];
    int currentValue = 1;
    int lastValue = 0;

    for(int i = 0; i < 4096 * 4096; i += 4096){
        lastValue = buffer[i];
        buffer[i] = currentValue;
    }
    currentValue += 1;

    return lastValue;
#else
    volatile int* ptr = (volatile int*) 0;
    int sum = 0;
    for(int i = 0; i < 4096 * 16; i += 4096){
        int val = ptr[i];
        sum += val;
    }
    return sum;
#endif
}

void versat_sha256(uint8_t *out, const uint8_t *in, size_t inlen);

static void store_bigendian_32(uint8_t *x, uint64_t u) {
    x[3] = (uint8_t) u;
    u >>= 8;
    x[2] = (uint8_t) u;
    u >>= 8;
    x[1] = (uint8_t) u;
    u >>= 8;
    x[0] = (uint8_t) u;
}

static uint initialStateValues[] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
static uint kConstants0[] = {0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174};
static uint kConstants1[] = {0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967};
static uint kConstants2[] = {0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070};
static uint kConstants3[] = {0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

static uint* kConstants[4] = {kConstants0,kConstants1,kConstants2,kConstants3};

#ifdef PC
    static char mem[1024*1024]; // 1 Mb
    #define DDR_MEM mem
#else
#if (RUN_DDR_SW==0)
    #define DDR_MEM (EXTRA_BASE)
#else
    #define DDR_MEM ((1<<(FIRM_ADDR_W)))
#endif
#endif

char GetHexadecimalChar(int value){
    if(value < 10){
        return '0' + value;
    } else{
        return 'a' + (value - 10);
    }
}

unsigned char* GetHexadecimal(const unsigned char* text, int str_size){
    static unsigned char buffer[2048+1];
    int i;

    for(i = 0; i< str_size; i++){
        if(i * 2 > 2048){
            printf("\n\n<GetHexadecimal> Maximum size reached\n\n");
            buffer[i*2] = '\0';
            return buffer;
        }

        int ch = (int) ((unsigned char) text[i]);

        buffer[i*2] = GetHexadecimalChar(ch / 16);
        buffer[i*2+1] = GetHexadecimalChar(ch % 16);
    }

    buffer[(i)*2] = '\0';

    return buffer;
}

static int unitFBits[] = {8};
static int readMemory_[64];
static int* readMemory;
static int writeMemory_[64];
static int* writeMemory;

// GLOBALS
Accelerator* accel;
bool initVersat = false;

FUDeclaration* MEM;
FUDeclaration* REG;

int* TestInstance(Accelerator* accel,FUInstance* inst,int numberInputs,int numberOutputs,...){
    static int out[99];
    FUInstance* inputs[99];
    FUInstance* outputs[99];

    va_list args;
    va_start(args,numberOutputs);

    #ifdef PC
    Assert(accel->instances.Size() == 1);
    #endif

    int registersAdded = 0;
    for(int i = 0; i < numberInputs; i++){

        char buffer[128];
        int size = snprintf(buffer,128,"regIn%d",registersAdded++);

        inputs[i] = CreateNamedFUInstance(accel,REG,MakeSizedString(buffer,size));

        int val = va_arg(args,int);

        VersatUnitWrite(inputs[i],0,val);

        ConnectUnits(inputs[i],0,inst,i);
    }

    registersAdded = 0;
    for(int i = 0; i < numberOutputs; i++){
        char buffer[128];
        int size = snprintf(buffer,128,"regOut%d",registersAdded++);
        outputs[i] = CreateNamedFUInstance(accel,REG,MakeSizedString(buffer,size));

        ConnectUnits(inst,i,outputs[i],0);
    }

    CalculateDelay(accel->versat,accel);
    SetDelayRecursive(accel);

    #if 0
    Accelerator* flatten = Flatten(accel->versat,accel,1);

    {
    FILE* dotFile = fopen("flatten.dot","w");
    OutputGraphDotFile(flatten,dotFile,1);
    fclose(dotFile);
    }
    #endif

    OutputVersatSource(accel->versat,accel,"versat_instance.v","versat_defs.vh","versat_data.inc");

    //Hook(accel->versat,accel);

    AcceleratorRun(accel);

    for(int i = 0; i < numberInputs; i++){
        RemoveFUInstance(accel,inputs[i]);
    }

    for(int i = 0; i < numberOutputs; i++){
        out[i] = outputs[i]->state[0];

        RemoveFUInstance(accel,outputs[i]);
    }

    #ifdef PC
    Assert(accel->instances.Size() == 1);
    #endif

    va_end(args);

    return out;
}

int* TestSequentialInstance(Accelerator* accel,FUInstance* inst,int numberValues,int numberOutputs,...){
    static int out[99];
    FUInstance* outputs[99];

    va_list args;
    va_start(args,numberOutputs);

    FUInstance* input = CreateNamedFUInstance(accel,MEM,MAKE_SIZED_STRING("memIn"));

    ConnectUnits(input,0,inst,0);
    {
        volatile MemConfig* c = (volatile MemConfig*) input->config;

        c->iterA = 1;
        c->incrA = 1;
        c->perA = numberValues;
        c->dutyA = numberValues;
    }

    int registersAdded = 0;
    {
    TIME_IT('F');
    for(int i = 0; i < numberValues; i++){
        int val = va_arg(args,int);

        VersatUnitWrite(input,i,val);
    }

    registersAdded = 0;
    for(int i = 0; i < numberOutputs; i++){
        char buffer[128];
        int size = snprintf(buffer,128,"regOut%d",registersAdded++);
        outputs[i] = CreateNamedFUInstance(accel,REG,MakeSizedString(buffer,size));

        ConnectUnits(inst,i,outputs[i],0);
    }
    }

    CalculateDelay(accel->versat,accel);
    SetDelayRecursive(accel);

    #if 1
    OutputGraphDotFile(accel,true,"circuit.dot");

    Accelerator* flatten = Flatten(accel->versat,accel,1);

    OutputGraphDotFile(flatten,true,"flatten.dot");
    #endif

    OutputVersatSource(accel->versat,accel,"versat_instance.v","versat_defs.vh","versat_data.inc");

    #if 1
    OutputMemoryMap(accel->versat,accel);
    #endif

    {
    TIME_IT('F');
    AcceleratorRun(accel);
    }

    RemoveFUInstance(accel,input);

    for(int i = 0; i < numberOutputs; i++){
        out[i] = outputs[i]->state[0];

        RemoveFUInstance(accel,outputs[i]);
    }

    va_end(args);

    return out;
}

void TestMStage(Versat* versat){
    FUDeclaration* type = GetTypeByName(versat,MakeSizedString("M_Stage"));
    Accelerator* accel = CreateAccelerator(versat);
    FUInstance* inst = CreateNamedFUInstance(accel,type,MAKE_SIZED_STRING("Test"));

    SetDelayRecursive(accel);

    int constants[] = {7,18,3,17,19,10};
    for(int i = 0; i < ARRAY_SIZE(constants); i++){
        inst->config[i] = constants[i];
    }

    int* out = TestInstance(accel,inst,4,1,0x5a86b737,0xa9f9be83,0x08251f6d,0xeaea8ee9);

    printf("Expected: 0xb89ab4ca\n");
    printf("Got:      0x%x\n",out[0]);
}

void TestFStage(Versat* versat){
    FUDeclaration* type = GetTypeByName(versat,MakeSizedString("F_Stage"));
    Accelerator* accel = CreateAccelerator(versat);
    FUInstance* inst = CreateNamedFUInstance(accel,type,MAKE_SIZED_STRING("Test"));

    SetDelayRecursive(accel);

    int constants[] = {6,11,25,2,13,22};
    for(int i = 0; i < ARRAY_SIZE(constants); i++){
        inst->config[i] = constants[i];
    }

    int* out = TestInstance(accel,inst,10,8,0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19,0x428a2f98,0x5a86b737);

    printf("0x568f3f84 0x6a09e667 0xbb67ae85 0x3c6ef372 0xf34e99d9 0x510e527f 0x9b05688c 0x1f83d9ab 0x428a2f98 0x5a86b737\n");
    for(int i = 0; i < 8; i++){
        printf("0x%08x ",out[i]);
    }
    printf("\n");
}

void TestInputM(Versat* versat){
    FUInstance* inst;
    {
    TIME_IT('F');
    FUDeclaration* type = GetTypeByName(versat,MakeSizedString("M"));
    accel = CreateAccelerator(versat);
    inst = CreateNamedFUInstance(accel,type,MAKE_SIZED_STRING("Test"));

    SetDelayRecursive(accel);

    #if 1
    OutputGraphDotFile(type->circuit,true,"circuit.dot");
    #endif
    }

    int constants[] = {7,18,3,17,19,10};

    // Set constants to every entity
    {
    TIME_IT('F');
    for(int i = 0; i < 16; i++){
        for(int ii = 0; ii < ARRAY_SIZE(constants); ii++){
            inst->config[i*6+ii] = constants[ii];
        }
    }
    }

    int* out = TestSequentialInstance(accel,inst,16,1,0x5a86b737,0xeaea8ee9,0x76a0a24d,0xa63e7ed7,0xeefad18a,0x101c1211,0xe2b3650c,0x5187c2a8,0xa6505472,0x08251f6d,0x4237e661,0xc7bf4c77,0xf3353903,0x94c37fa1,0xa9f9be83,0x6ac28509);

    printf("b89ab4ca fc0ba687 6f70775f fd7fcf73 ddc5d5d7 b54ee23e 481631f5 9c325ada 1e01af58 11016b62 465da978 961e5ee7 9860640b 3f309ec4 439e4f9d 14ca5690\n");
    for(int i = 0; i < 16; i++){
        printf("%08x ",out[i]);
    }
    printf("\n");
}

void InstantiateSHA(Versat* versat){
    FUInstance* inst = nullptr;
    {
    TIME_IT('F');
    FUDeclaration* type = GetTypeByName(versat,MakeSizedString("SHA"));
    accel = CreateAccelerator(versat);
    inst = CreateNamedFUInstance(accel,type,MAKE_SIZED_STRING("SHA"));

    OutputUnitInfo(inst);

    FUInstance* read = GetInstanceByName(accel,"SHA","MemRead");
    {
        volatile VReadConfig* c = (volatile VReadConfig*) read->config;

        // Versat side
        c->iterB = 1;
        c->incrB = 1;
        c->perB = 16;
        c->dutyB = 16;

        // Memory side
        c->incrA = 1;
        c->iterA = 1;
        c->perA = 16;
        c->dutyA = 16;
        c->size = 8;
        c->int_addr = 0;
        c->pingPong = 1;
        c->ext_addr = (int) readMemory; // Some place so no segfault if left unconfigured
    }

    for(int i = 0; i < 4; i++){
        FUInstance* mem = GetInstanceByName(accel,"SHA","cMem%d",i,"mem");

        for(int ii = 0; ii < 16; ii++){
            VersatUnitWrite(mem,ii,kConstants[i][ii]);
        }
    }
    }

    CalculateDelay(versat,accel);
    SetDelayRecursive(accel);
}

void TestSHA(Versat* versat){
    InstantiateSHA(versat);

    unsigned char digest[256];
    for(int i = 0; i < 256; i++){
        digest[i] = 0;
    }

    Hook(versat,nullptr,nullptr);

    printf("Expected: 42e61e174fbb3897d6dd6cef3dd2802fe67b331953b06114a65c772859dfc1aa\n");
    versat_sha256(digest,msg_64,64);
    printf("Result:   %s\n",GetHexadecimal(digest, HASH_SIZE));

    // Gera o versat.
    OutputVersatSource(versat,accel,"versat_instance.v","versat_defs.vh","versat_data.inc");
}

void MatrixMultiplication(){
    #define DIM 4
    int matrixA[DIM*DIM];
    int matrixB[DIM*DIM];
    int res[DIM*DIM];

    int size = DIM * DIM;

    for(int i = 0; i < size; i++){
        matrixA[i] = i + 1;
        matrixB[i] = i + 1;
    }

    {
    #ifndef PC
    MEMSET(VERSAT_BASE,0x0,0);
    #endif

    for(int y = 0; y < DIM; y++){
        for(int x = 0; x < DIM; x++){
            int sum = 0;
            for(int i = 0; i < DIM; i++){
                sum += matrixA[y * DIM + i] * matrixB[i * DIM + x];
            }
            res[y * DIM + x] = sum;
        }
    }
    #ifndef PC
    MEMSET(VERSAT_BASE,0x0,0);
    #endif

    }

    #if 0
    for(int y = 0; y < DIM; y++){
        for(int x = 0; x < DIM; x++){
            printf("%d ",res[y][x]);
        }
        printf("\n");
    }
    #endif
}

void ConfigureSimpleVRead(FUInstance* inst, int numberItems,int* memory){
    volatile VReadConfig* c = (volatile VReadConfig*) inst->config;

    // Memory side
    c->incrA = 1;
    c->iterA = 1;
    c->perA = numberItems;
    c->dutyA = numberItems;
    c->size = 8;
    c->int_addr = 0;
    c->pingPong = 1;

    c->ext_addr = (int) memory;
}

void ConfigureLeftSideMatrix(FUInstance* inst,int iterations){
    volatile MemConfig* config = (volatile MemConfig*) inst->config;

    config->iterA = iterations;
    config->perA = iterations;
    config->dutyA = iterations;
    config->startA = 0;
    config->shiftA = -iterations;
    config->incrA = 1;
    config->reverseA = 0;
    config->iter2A = 1;
    config->per2A = iterations;
    config->shift2A = 0;
    config->incr2A = iterations;
}

void ConfigureRightSideMatrix(FUInstance* inst, int iterations){
    volatile MemConfig* config = (volatile MemConfig*) inst->config;

    config->iterA = iterations;
    config->perA = iterations;
    config->dutyA = iterations;
    config->startA = 0;
    config->shiftA = -(iterations * iterations - 1);
    config->incrA = iterations;
    config->reverseA = 0;
    config->iter2A = 1;
    config->per2A = iterations;
    config->shift2A = 0;
    config->incr2A = 0;
}

void ConfigureMemoryLinear(FUInstance* inst, int amountOfData){
    volatile MemConfig* config = (volatile MemConfig*) inst->config;

    config->iterA = 1;
    config->perA = amountOfData;
    config->dutyA = amountOfData;
    config->incrA = 1;
}

void ConfigureMemoryReceive(FUInstance* inst, int amountOfData,int interdataDelay){
    volatile MemConfig* config = (volatile MemConfig*) inst->config;

    config->iterA = amountOfData;
    config->perA = interdataDelay;
    config->dutyA = 1;
    config->startA = 0;
    config->shiftA = 0;
    config->incrA = 1;
    config->in0_wr = 1;
    config->reverseA = 0;
    config->iter2A = 0;
    config->per2A = 0;
    config->shift2A = 0;
    config->incr2A = 0;
}

void TestMemory(Versat* versat){
    Accelerator* accel = CreateAccelerator(versat);
    FUDeclaration* type = GetTypeByName(versat,MAKE_SIZED_STRING("memViewer"));
    FUInstance* inst = CreateNamedFUInstance(accel,type,MAKE_SIZED_STRING("test"));

    FUInstance* mem = GetInstanceByName(accel,"test","memory");

    ConfigureRightSideMatrix(mem,2);

    for(int i = 0; i < 16; i++){
        VersatUnitWrite(mem,i,i);
    }

    VersatUnitWrite(mem,1020,0xfc);
    VersatUnitWrite(mem,1021,0xfd);
    VersatUnitWrite(mem,1022,0xfe);
    VersatUnitWrite(mem,1023,0xff);

    CalculateDelay(versat,accel);
    SetDelayRecursive(accel);

    for(int i = 0; i < 2; i++){
        for(int j = 0; j < 2; j++){
            printf("%d\n",VersatUnitRead(mem,i*2+j));
        }
    }

    AcceleratorRun(accel);

    #if 0
    FUInstance* reg = GetInstanceByName(accel,"test","d1");
    printf("%d\n",reg->state[0]);
    #endif

    OutputVersatSource(versat,accel,"versat_instance.v","versat_defs.vh","versat_data.inc");
}

void ConfigureLeftSideMatrixVRead(FUInstance* inst, int iterations){
    volatile VReadConfig* config = (volatile VReadConfig*) inst->config;

    int numberItems = iterations * iterations;

    config->incrA = 1;
    config->iterA = 1;
    config->perA = numberItems;
    config->dutyA = numberItems;
    config->size = 8;
    config->int_addr = 0;
    config->pingPong = 1;

    config->iterB = iterations;
    config->perB = iterations;
    config->dutyB = iterations;
    config->startB = 0;
    config->shiftB = -iterations;
    config->incrB = 1;
    config->reverseB = 0;
    config->iter2B = 1;
    config->per2B = iterations;
    config->shift2B = 0;
    config->incr2B = iterations;
}

void ConfigureRightSideMatrixVRead(FUInstance* inst, int iterations){
    volatile VReadConfig* config = (volatile VReadConfig*) inst->config;

    int numberItems = iterations * iterations;

    config->incrA = 1;
    config->iterA = 1;
    config->perA = numberItems;
    config->dutyA = numberItems;
    config->size = 8;
    config->int_addr = 0;
    config->pingPong = 1;

    config->iterB = iterations;
    config->perB = iterations;
    config->dutyB = iterations;
    config->startB = 0;
    config->shiftB = -(iterations * iterations - 1);
    config->incrB = iterations;
    config->reverseB = 0;
    config->iter2B = 1;
    config->per2B = iterations;
    config->shift2B = 0;
    config->incr2B = 0;
}

void ConfigureMatrixVWrite(FUInstance* inst,int amountOfData){
    volatile VWriteConfig* config = (volatile VWriteConfig*) inst->config;

    config->incrA = 1;
    config->iterA = 1;
    config->perA = amountOfData;
    config->dutyA = amountOfData;
    config->size = 8;
    config->int_addr = 0;
    config->pingPong = 1;

    config->iterB = amountOfData;
    config->perB = 4;
    config->dutyB = 1;
    config->incrB = 1;
}

void VersatMatrixMultiplicationVRead(Versat* versat){
    #define DIM 4
    int matrixA[DIM*DIM];
    int matrixB[DIM*DIM];
    int matrixRes[DIM*DIM];
    volatile int* resPtr = (volatile int*) matrixRes;

    Accelerator* accel = CreateAccelerator(versat);
    FUDeclaration* type = GetTypeByName(versat,MAKE_SIZED_STRING("MatrixMultiplicationVread"));
    FUInstance* inst = CreateNamedFUInstance(accel,type,MAKE_SIZED_STRING("test"));

    FUInstance* memA = GetInstanceByName(accel,"test","matA");
    FUInstance* memB = GetInstanceByName(accel,"test","matB");
    FUInstance* muladd = GetInstanceByName(accel,"test","ma");

    FUInstance* res = GetInstanceByName(accel,"test","res");

    int dimensions = DIM;
    int size = dimensions * dimensions;

    ConfigureLeftSideMatrixVRead(memA,dimensions);
    ConfigureRightSideMatrixVRead(memB,dimensions);

    {
    volatile VReadConfig* config = (volatile VReadConfig*) memA->config;
    config->ext_addr = (int) matrixA;
    }

    {
    volatile VReadConfig* config = (volatile VReadConfig*) memB->config;
    config->ext_addr = (int) matrixB;
    }

    for(int i = 0; i < size; i++){
        matrixA[i] = i + 1;
        matrixB[i] = i + 1;
    }

    volatile MuladdConfig* conf = (volatile MuladdConfig*) muladd->config;

    conf->opcode = 0;
    conf->iterations = size;
    conf->period = dimensions;
    conf->shift = 0;

    CalculateDelay(versat,accel);
    SetDelayRecursive(accel);

    ConfigureMatrixVWrite(res,size);
    {
    volatile VWriteConfig* config = (volatile VWriteConfig*) res->config;
    config->ext_addr = (int) matrixRes;
    printf("%x\n",(int) matrixRes);
    }

    AcceleratorRun(accel);
    AcceleratorRun(accel);
    AcceleratorRun(accel);

    ClearCache();

    for(int i = 0; i < dimensions; i++){
        for(int j = 0; j < dimensions; j++){
        printf("%d ",resPtr[i*dimensions + j]);
        }
        printf("\n");
    }

    OutputVersatSource(versat,accel,"versat_instance.v","versat_defs.vh","versat_data.inc");
}

void InstantiateMatrixOverMerge(Versat* versat,Accelerator* accel){
    #define DIM 4
    int matrixA[DIM*DIM];
    int matrixB[DIM*DIM];
    int matrixRes[DIM*DIM];
    volatile int* resPtr = (volatile int*) matrixRes;

    FUInstance* memA = GetInstanceByName(accel,"test","pixels0");
    FUInstance* memB = GetInstanceByName(accel,"test","pixels1");
    FUInstance* muladd = GetInstanceByName(accel,"test","ma");

    FUInstance* res = GetInstanceByName(accel,"test","matrixRes");

    #if 1
    CalculateDelay(versat,accel);
    SetDelayRecursive(accel);
    #endif

    #if 0
    memA->delay[0] = 0;
    memB->delay[0] = 0;
    muladd->delay[0] = 1;
    res->delay[0] = 4;
    #endif

    #if 0
    memA->delay[0] = 0;
    memB->delay[0] = 4;
    muladd->delay[0] = 15;
    res->delay[0] = 18;
    #endif

    int dimensions = DIM;
    int size = dimensions * dimensions;

    ConfigureLeftSideMatrixVRead(memA,dimensions);
    ConfigureRightSideMatrixVRead(memB,dimensions);

    {
    volatile VReadConfig* config = (volatile VReadConfig*) memA->config;
    config->ext_addr = (int) matrixA;
    }

    {
    volatile VReadConfig* config = (volatile VReadConfig*) memB->config;
    config->ext_addr = (int) matrixB;
    }

    for(int i = 0; i < size; i++){
        matrixA[i] = i + 1;
        matrixB[i] = i + 1;
        //matrixRes[i] = 0;
    }

    volatile MuladdConfig* conf = (volatile MuladdConfig*) muladd->config;

    conf->opcode = 0;
    conf->iterations = size;
    conf->period = dimensions;
    conf->shift = 0;

    ConfigureMatrixVWrite(res,size);
    {
    volatile VWriteConfig* config = (volatile VWriteConfig*) res->config;
    config->ext_addr = (int) matrixRes;
    printf("%x\n",(int) matrixRes);
    }

    AcceleratorRun(accel);
    AcceleratorRun(accel);
    AcceleratorRun(accel);

    ClearCache();

    for(int i = 0; i < dimensions; i++){
        for(int j = 0; j < dimensions; j++){
        printf("%d ",resPtr[i*dimensions + j]);
        }
        printf("\n");
    }
}

void VersatMatrixMultiplication(Versat* versat){
    Accelerator* accel = CreateAccelerator(versat);
    FUDeclaration* type = GetTypeByName(versat,MAKE_SIZED_STRING("MatrixMultiplication"));
    FUInstance* inst = CreateNamedFUInstance(accel,type,MAKE_SIZED_STRING("test"));

    FUInstance* memA = GetInstanceByName(accel,"test","matA");
    FUInstance* memB = GetInstanceByName(accel,"test","matB");
    FUInstance* muladd = GetInstanceByName(accel,"test","ma");

    FUInstance* res = GetInstanceByName(accel,"test","res");

    int dimensions = 4;
    int size = dimensions * dimensions;

    ConfigureLeftSideMatrix(memA,dimensions);
    ConfigureRightSideMatrix(memB,dimensions);

    for(int i = 0; i < size; i++){
        VersatUnitWrite(memA,i,i+1);
        VersatUnitWrite(memB,i,i+1);
    }

    volatile MuladdConfig* conf = (volatile MuladdConfig*) muladd->config;

    conf->opcode = 0;
    conf->iterations = size;
    conf->period = dimensions;
    conf->shift = 0;

    CalculateDelay(versat,accel);
    SetDelayRecursive(accel);

    ConfigureMemoryReceive(res,size,dimensions);

    AcceleratorRun(accel);

    for(int i = 0; i < dimensions; i++){
        for(int j = 0; j < dimensions; j++){
        printf("%d ",VersatUnitRead(res,i*dimensions + j));
        }
        printf("\n");
    }

    OutputVersatSource(versat,accel,"versat_instance.v","versat_defs.vh","versat_data.inc");
}

#define nSTAGE 5

void Convolution(){
    int i, j, k, l, m;
    int pixels[25 * nSTAGE], weights[9 * nSTAGE], bias = 0, res;

    srand(0);
    //write data in versat mems
    for (j = 0; j < nSTAGE; j++){
        //write 5x5 feature map in mem0
        for (i = 0; i < 25; i++)
        {
            pixels[25 * j + i] = rand() % 50 - 25;
        }

        //write 3x3 kernel and bias in mem1
        for (i = 0; i < 9; i++)
        {
            weights[9 * j + i] = rand() % 10 - 5;
        }

        //write bias after weights of VERSAT 0
        if(j == 0)
        {
            bias = rand() % 20 - 10;
        }
  }
  //expected result of 3D convolution
    for (i = 0; i < 3; i++){
        for (j = 0; j < 3; j++){
          res = bias;
          for (k = 0; k < nSTAGE; k++)
          {
            for (l = 0; l < 3; l++)
            {
              for (m = 0; m < 3; m++)
              {
                res += pixels[i * 5 + j + k * 25 + l * 5 + m] * weights[9 * k + l * 3 + m];
              }
            }
          }
        }
    }
}

void VersatConvolution(Versat* versat){
    int pixels[25 * nSTAGE], weights[9 * nSTAGE], bias = 0;

    srand(0);
    //write data in versat mems
    for (int j = 0; j < nSTAGE; j++){
        //write 5x5 feature map in mem0
        for (int i = 0; i < 25; i++)
        {
            pixels[25 * j + i] = rand() % 50 - 25;
        }

        //write 3x3 kernel and bias in mem1
        for (int i = 0; i < 9; i++)
        {
            weights[9 * j + i] = rand() % 10 - 5;
        }

        //write bias after weights of VERSAT 0
        if(j == 0)
        {
            bias = rand() % 20 - 10;
        }
    }
    ClearCache();

    Accelerator* accel = CreateAccelerator(versat);
    FUDeclaration* type = GetTypeByName(versat,MAKE_SIZED_STRING("Convolution"));
    FUInstance* inst = CreateNamedFUInstance(accel,type,MAKE_SIZED_STRING("test"));

    int i, j, k, l, m;

    volatile MemConfig* pixelConfig = nullptr;
    volatile MemConfig* weightsConfig = nullptr;
    //write data in versat mems
    volatile VReadConfig* pixelConfigs[5];
    for (j = 0; j < nSTAGE; j++){
        FUInstance* pixel = GetInstanceByName(accel,"test","stage%d",j,"pixels");

        ConfigureSimpleVRead(pixel,25,&pixels[25*j]);
        {
            volatile VReadConfig* config = (volatile VReadConfig*) pixel->config;
            pixelConfigs[j] = config;

            // B - versat side
            config->iterB = 3;
            config->incrB = 1;
            config->perB = 3;
            config->dutyB = 3;
            config->shiftB = 5 - 3;
        }

        #if 0
        //write 5x5 feature map in mem0
        for (i = 0; i < 25; i++){
            //VersatUnitWrite(pixels,i, rand() % 50 - 25);
        }
        #endif

        FUInstance* weight = GetInstanceByName(accel,"test","stage%d",j,"weights");

        //write 3x3 kernel and bias in mem1
        for (i = 0; i < 9; i++){
            VersatUnitWrite(weight,i, weights[9*j + i]);
        }

        //write bias after weights of VERSAT 0
        if(j == 0){
            FUInstance* bia = GetInstanceByName(accel,"test","bias");
            bia->config[0] = bias;

            {
                volatile MemConfig* config = (volatile MemConfig*) weight->config;
                weightsConfig = config;

                ConfigureMemoryLinear(weight,9);
            }

            {
                FUInstance* muladd = GetInstanceByName(accel,"test","stage0","muladd");

                volatile MuladdConfig* config = (volatile MuladdConfig*) muladd->config;

                config->iterations = 1;
                config->period = 9;
            }
        }
    }

    FUInstance* res = GetInstanceByName(accel,"test","res");

    volatile MemConfig* resConfig = (volatile MemConfig*) res->config;

    resConfig->iterA = 1;
    resConfig->incrA = 1;
    resConfig->perA = 1;
    resConfig->dutyA = 1;
    resConfig->in0_wr = 1;

    CalculateDelay(versat,accel);
    SetDelayRecursive(accel);

    AcceleratorRun(accel); // Load vreads with initial good data

    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            for(int x = 0; x < 5; x++){
                pixelConfigs[x]->startB = i * 5 + j;
            }

            resConfig->startA = i * 3 + j;

            AcceleratorRun(accel);
        }
    }

    for (i = 0; i < 3; i++){
        for (j = 0; j < 3; j++){
          printf("%d\t", VersatUnitRead(res,i * 3 + j));
        }
        printf("\n");
    }

    OutputVersatSource(versat,accel,"versat_instance.v","versat_defs.vh","versat_data.inc");
}

Accelerator* InstantiateMerge(Versat* versat){
    Accelerator* accel = CreateAccelerator(versat);
    FUDeclaration* type = GetTypeByName(versat,MAKE_SIZED_STRING("Merged"));
    FUInstance* inst = CreateNamedFUInstance(accel,type,MAKE_SIZED_STRING("test"));

    return accel;
}

void InstantiateConvolutionOverMerge(Versat* versat,Accelerator* accel){
    int pixels[25 * nSTAGE], weights[9 * nSTAGE], bias = 0;

    srand(0);
    //write data in versat mems
    for (int j = 0; j < nSTAGE; j++){
        //write 5x5 feature map in mem0
        for (int i = 0; i < 25; i++)
        {
            pixels[25 * j + i] = rand() % 50 - 25;
        }

        //write 3x3 kernel and bias in mem1
        for (int i = 0; i < 9; i++)
        {
            weights[9 * j + i] = rand() % 10 - 5;
        }

        //write bias after weights of VERSAT 0
        if(j == 0)
        {
            bias = rand() % 20 - 10;
        }
    }
    ClearCache();

    //write data in versat mems
    volatile VReadConfig* pixelConfigs[5];
    for (int j = 0; j < nSTAGE; j++){
        FUInstance* pixel = GetInstanceByName(accel,"test","pixels%d",j);

        ConfigureSimpleVRead(pixel,25,&pixels[25*j]);
        {
            volatile VReadConfig* config = (volatile VReadConfig*) pixel->config;
            pixelConfigs[j] = config;

            // B - versat side
            config->iterB = 3;
            config->incrB = 1;
            config->perB = 3;
            config->dutyB = 3;
            config->shiftB = 5 - 3;
        }

        FUInstance* weight = GetInstanceByName(accel,"test","weights%d",j);

        //write 3x3 kernel and bias in mem1
        for (int i = 0; i < 9; i++){
            VersatUnitWrite(weight,i, weights[9*j + i]);
        }

        {
            volatile MemConfig* config = (volatile MemConfig*) weight->config;
            ConfigureMemoryLinear(weight,9);
        }

        FUInstance* muladd = GetInstanceByName(accel,"test","muladd%d",j);
        {
            volatile MuladdConfig* config = (volatile MuladdConfig*) muladd->config;

            config->iterations = 1;
            config->period = 9;
        }
    }

    FUInstance* bia = GetInstanceByName(accel,"test","bias");
    bia->config[0] = bias;

    FUInstance* res = GetInstanceByName(accel,"test","ConvolutionRes");

    volatile MemConfig* resConfig = (volatile MemConfig*) res->config;

    resConfig->iterA = 1;
    resConfig->incrA = 1;
    resConfig->perA = 1;
    resConfig->dutyA = 1;
    resConfig->in0_wr = 1;

    CalculateDelay(versat,accel);
    SetDelayRecursive(accel);

    AcceleratorRun(accel); // Load vreads with initial good data

    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            for(int x = 0; x < 5; x++){
                pixelConfigs[x]->startB = i * 5 + j;
            }

            resConfig->startA = i * 3 + j;

            AcceleratorRun(accel);
        }
    }

    for (int i = 0; i < 3; i++){
        for (int j = 0; j < 3; j++){
          printf("%d\t", VersatUnitRead(res,i * 3 + j));
        }
        printf("\n");
    }
}

int CalculateSum(char* buffer,int* weights){
   int sum = 0;

   for(int j = 0; j < 5; j++){
      sum +=  buffer[j] * weights[j];
   }

   return sum;
}

int weights[] = {17,67,109,157,199};
char testString[] = "123249819835894981389Waldo198239812849825899904924oefhcasjngwoeijfjvakjndcoiqwj";

void StringHasher(){
    #ifndef PC
    MEMSET(VERSAT_BASE,0x0,0);
    #endif
    int SumToFind = CalculateSum("Waldo",weights);

    for(int i = 0; i < sizeof(testString) - 5; i++){
        int sum = CalculateSum(&testString[i],weights);
    }
    #ifndef PC
    MEMSET(VERSAT_BASE,0x0,0);
    #endif
}

void VersatStringHasher(Versat* versat){
    Accelerator* accel = CreateAccelerator(versat);
    FUDeclaration* type = GetTypeByName(versat,MAKE_SIZED_STRING("StringHasher"));
    FUInstance* inst = CreateNamedFUInstance(accel,type,MAKE_SIZED_STRING("test"));

    FUInstance* muladd = GetInstanceByName(accel,"test","mul1","mul");

    volatile MuladdConfig* conf = (volatile MuladdConfig*) muladd->config;
    conf->opcode = 0;
    conf->iterations = 99;
    conf->period = 1;
    conf->shift = 0;

    for(int i = 0; i < 5; i++){
        inst->config[i] = weights[i];
    }

    FUInstance* bytesIn = GetInstanceByName(accel,"test","bytesIn");
    FUInstance* bytesOut = GetInstanceByName(accel,"test","bytesOut");

    CalculateDelay(versat,accel);
    SetDelayRecursive(accel);

    for(int i = 0; i < 5; i++){
        VersatUnitWrite(bytesIn,i,(int) ("Waldo"[i]));
    }

    ConfigureMemoryLinear(bytesIn,5);
    ConfigureMemoryReceive(bytesOut,1,1);

    AcceleratorRun(accel);

    int hash = VersatUnitRead(bytesOut,0);

    for(int i = 0; i < sizeof(testString); i++){
        VersatUnitWrite(bytesIn,i,(int) testString[i]);
    }

    ConfigureMemoryLinear(bytesIn,sizeof(testString));
    ConfigureMemoryReceive(bytesOut,sizeof(testString)-5,1);

    AcceleratorRun(accel);

    #if 0
    for(int i = 0; i < sizeof(testString) - 5; i++){
        printf("%d:%d (%d)\n",i,VersatUnitRead(bytesOut,i),CalculateSum(&testString[i],weights));
    }
    #endif

    for(int i = 0; i < sizeof(testString) - 5; i++){
        int val = VersatUnitRead(bytesOut,i);

        if(hash == val){
            printf("%d - %.5s\n",i,&testString[i]);
        }
    }

    OutputVersatSource(versat,accel,"versat_instance.v","versat_defs.vh","versat_data.inc");
}

void InstantiateStringHasherOverMerge(Versat* versat,Accelerator* accel){
    for(int i = 0; i < 5; i++){
        FUInstance* muladd = GetInstanceByName(accel,"test","muladd%d",i);

        volatile MuladdConfig* conf = (volatile MuladdConfig*) muladd->config;
        conf->opcode = 0;
        conf->iterations = 99;
        conf->period = 1;
        conf->shift = 0;
    }

    for(int i = 0; i < 5; i++){
        FUInstance* w = GetInstanceByName(accel,"test","weights%d",i);

        VersatUnitWrite(w,0,weights[i]);

        volatile MemConfig* config = (volatile MemConfig*) w->config;

        config->iterA = 99;
        config->perA = 0;
        config->dutyA = 1;
    }

    FUInstance* bytesIn = GetInstanceByName(accel,"test","bytesIn");
    FUInstance* bytesOut = GetInstanceByName(accel,"test","ConvolutionRes");

    CalculateDelay(versat,accel);
    SetDelayRecursive(accel);

    for(int i = 0; i < 5; i++){
        VersatUnitWrite(bytesIn,i,(int) ("Waldo"[i]));
    }

    ConfigureMemoryLinear(bytesIn,5);
    ConfigureMemoryReceive(bytesOut,1,1);

    #if 0
    AcceleratorRun(accel);

    int hash = VersatUnitRead(bytesOut,0);

    for(int i = 0; i < sizeof(testString); i++){
        VersatUnitWrite(bytesIn,i,(int) testString[i]);
    }

    ConfigureMemoryLinear(bytesIn,sizeof(testString));
    ConfigureMemoryReceive(bytesOut,sizeof(testString)-5,1);

    AcceleratorRun(accel);

    #if 0
    for(int i = 0; i < sizeof(testString) - 5; i++){
        printf("%d:%d (%d)\n",i,VersatUnitRead(bytesOut,i),CalculateSum(&testString[i],weights));
    }
    #endif

    for(int i = 0; i < sizeof(testString) - 5; i++){
        int val = VersatUnitRead(bytesOut,i);

        if(hash == val){
            printf("%d - %.5s\n",i,&testString[i]);
        }
    }
    #endif
}

int main(int argc,const char* argv[])
{
    unsigned char digest[256];

    //init uart
    uart_init(UART_BASE,FREQ/BAUD);
    timer_init(TIMER_BASE);

    printf("Init base modules\n");

    for(int i = 0; i < 256; i++){
        digest[i] = 0;
    }

    Versat versatInst = {};
    Versat* versat = &versatInst;
    InitVersat(versat,VERSAT_BASE,1);

    printf("Init Versat\n");

    ParseCommandLineOptions(versat,argc,argv);

    MEM = GetTypeByName(versat,MakeSizedString("Mem"));
    REG = GetTypeByName(versat,MakeSizedString("Reg"));

    #ifdef PC
    ParseVersatSpecification(versat,"testVersatSpecification.txt");
    #endif

    #if 0
    StringHasher();
    #endif

    #if 0
    VersatStringHasher(versat);
    #endif

    #if 0
    printf("Before convolution\n");
    Convolution();
    #endif

    #if 0
    printf("Before versat\n");
    VersatConvolution(versat);
    #endif

    #if 0
    MatrixMultiplication();
    #endif

    #if 0
    VersatMatrixMultiplicationVRead(versat);
    #endif

    #if 0
    VersatMatrixMultiplication(versat);
    #endif

    #if 0
    Accelerator* merged = InstantiateMerge(versat);

        #if 0
            InstantiateConvolutionOverMerge(versat,merged);
        #endif

        #if 0
            InstantiateMatrixOverMerge(versat,merged);
        #endif

        #if 0
            InstantiateStringHasherOverMerge(versat,merged);
        #endif

    OutputVersatSource(versat,merged,"versat_instance.v","versat_defs.vh","versat_data.inc");
    #endif


    #if 0
    TestMemory(versat);
    #endif

    #if 0
    Hook(versat,nullptr,nullptr);
    TestMStage(versat);
    #endif

    #if 0
    TestFStage(versat);
    #endif

    #if 0
    TestInputM(versat);
    #endif

    #if 0
    TestDelay(versat);
    #endif

    #if 0
    printf("Expected: 42e61e174fbb3897d6dd6cef3dd2802fe67b331953b06114a65c772859dfc1aa\n");
    sha256(digest,msg_64,64);
    printf("Result:   %s\n",GetHexadecimal(digest, HASH_SIZE));
    #endif

    #if 0
    TestDelayImprove(versat);
    #endif

    #if 0
    Hook(versat);
    #endif

    #if 1
    TestSHA(versat);
    #endif

    #if 0
    char key[16];
    memset(key,0,16);

    struct AES_ctx ctx;
    AES_init_ctx(&ctx,key);

    char msg[16] = {0xf3,0x44,0x81,0xec,0x3c,0xc6,0x27,0xba,0xcd,0x5d,0xc3,0xfb,0x08,0xf2,0x73,0xe6};

    AES_ECB_encrypt(&ctx,msg);

    printf("%s\n",GetHexadecimal(msg,16));
    printf("0336763e966d92595a567cc9ce537f5e\n");
    #endif

    #if 0
    printf("Expected: 42e61e174fbb3897d6dd6cef3dd2802fe67b331953b06114a65c772859dfc1aa\n");
    versat_sha256(digest,msg_64,64);
    printf("Result:   %s\n",GetHexadecimal(digest, HASH_SIZE));
    #endif

#if AUTOMATIC_TEST == 1
    int i = 0;

    InstantiateSHA(versat);

    printf("[L = %d]\n", HASH_SIZE);

    //Message test loop
    for(i=0; i< NUM_MSGS; i++){
        versat_sha256(digest,msg_array[i],msg_len[i]);
        printf("\nLen = %d\n", msg_len[i]*8);
        printf("Msg = %s\n", GetHexadecimal(msg_array[i], (msg_len[i]) ? msg_len[i] : 1));
        printf("MD = %s\n",GetHexadecimal(digest, HASH_SIZE));
    }
    printf("\n");
#endif

    uart_finish();

    return 0;
}

static uint load_bigendian_32(const uint8_t *x) {
     return (uint)(x[3]) | (((uint)(x[2])) << 8) |
                (((uint)(x[1])) << 16) | (((uint)(x[0])) << 24);
}

static size_t versat_crypto_hashblocks_sha256(const uint8_t *in, size_t inlen) {
    uint w[16];

    {
        FUInstance* read = GetInstanceByName(accel,"SHA","MemRead");

        volatile VReadConfig* c = (volatile VReadConfig*) read->config;
        c->ext_addr = (int) w;
    }

    while (inlen >= 64) {
        w[0]  = load_bigendian_32(in + 0);
        w[1]  = load_bigendian_32(in + 4);
        w[2]  = load_bigendian_32(in + 8);
        w[3]  = load_bigendian_32(in + 12);
        w[4]  = load_bigendian_32(in + 16);
        w[5]  = load_bigendian_32(in + 20);
        w[6]  = load_bigendian_32(in + 24);
        w[7]  = load_bigendian_32(in + 28);
        w[8]  = load_bigendian_32(in + 32);
        w[9]  = load_bigendian_32(in + 36);
        w[10] = load_bigendian_32(in + 40);
        w[11] = load_bigendian_32(in + 44);
        w[12] = load_bigendian_32(in + 48);
        w[13] = load_bigendian_32(in + 52);
        w[14] = load_bigendian_32(in + 56);
        w[15] = load_bigendian_32(in + 60);

        // Loads data + performs work
        AcceleratorRun(accel);

        #if 1
        if(!initVersat){
            for(int i = 0; i < 8; i++){
                FUInstance* inst = GetInstanceByName(accel,"SHA","State","s%d",i,"reg");
                VersatUnitWrite(inst,0,initialStateValues[i]);
            }
            initVersat = true;
        }
        #endif

        in += 64;
        inlen -= 64;
    }

    return inlen;
}

void versat_sha256(uint8_t *out, const uint8_t *in, size_t inlen) {
    uint8_t padded[128];
    uint64_t bytes = 0 + inlen;

    versat_crypto_hashblocks_sha256(in, inlen);
    in += inlen;
    inlen &= 63;
    in -= inlen;

    for (size_t i = 0; i < inlen; ++i) {
        padded[i] = in[i];
    }
    padded[inlen] = 0x80;

    if (inlen < 56) {
        for (size_t i = inlen + 1; i < 56; ++i) {
            padded[i] = 0;
        }
        padded[56] = (uint8_t) (bytes >> 53);
        padded[57] = (uint8_t) (bytes >> 45);
        padded[58] = (uint8_t) (bytes >> 37);
        padded[59] = (uint8_t) (bytes >> 29);
        padded[60] = (uint8_t) (bytes >> 21);
        padded[61] = (uint8_t) (bytes >> 13);
        padded[62] = (uint8_t) (bytes >> 5);
        padded[63] = (uint8_t) (bytes << 3);
        versat_crypto_hashblocks_sha256(padded, 64);
    } else {
        for (size_t i = inlen + 1; i < 120; ++i) {
            padded[i] = 0;
        }
        padded[120] = (uint8_t) (bytes >> 53);
        padded[121] = (uint8_t) (bytes >> 45);
        padded[122] = (uint8_t) (bytes >> 37);
        padded[123] = (uint8_t) (bytes >> 29);
        padded[124] = (uint8_t) (bytes >> 21);
        padded[125] = (uint8_t) (bytes >> 13);
        padded[126] = (uint8_t) (bytes >> 5);
        padded[127] = (uint8_t) (bytes << 3);
        versat_crypto_hashblocks_sha256(padded, 128);
    }

    // Does the last run with valid data
    AcceleratorRun(accel);

    for (size_t i = 0; i < 8; ++i) {
        FUInstance* inst = GetInstanceByName(accel,"SHA","State","s%d",i,"reg");

        uint val = *inst->state;

        store_bigendian_32(&out[i*4],val);
    }

    initVersat = false; // At the end of each run, reset
}

#endif

/*

Currently plan:

Things to do:

SHA implementation with macroinstances
AES implementation.

Keep track off:

The delay value for delay units is how much to extend latency, while delay for the other units is how many cycles before valid data arrives. (Made more concrete be setting the delay on a delay unit to be a configuration)
Remove instances doesn't update the config / state / memMapped / delay pointers

*/

/*

Implementation to do:

Overall:

    Start implementing error reporting. Some things should give error but the program still goes on, put asserts.
    Errors should be simple: Report the error and then quit. Must errors cannot be carry over. No fail and retry

Software:

    Change hierarchical name from a char[] to a char* (Otherwise will bleed embedded memory dry)
        Software for now will simply malloc and forget, but eventually I should probably implement string interning
        NOTE: After adding perfect hashing, name information is no longer required. Might not need to change afterall, for now hold on.

    Add true hierarchical naming for Flatten units
        - Simply get the full hierarchical representation and store it as the name of the unit, with parent set to null
        - Might need to change hierarchical name from array to char*
        - Need to take care about where strings end up. Do not want to fill embedded with useless data if possible

    Take a look at compute units that need delay information (think multiply accumulate, not delay but acts same way) [should simple by set inputDelay as delay]
    Support the output of a module not starting at zero. (Maybe weird thing to do for now)
        More complex, but take a pass at anything that depends on order of instancing/declarating (a lot of assumptions are being made right now)
    Go back and check the output memory map for individual accelerators, there is some bugs for the memory allocation right now
    Lock accelerator can resize memory allocated to fit exactly with amount of memory needed
    [Type] Change ouput memory map and functions alike to use type system and simple output structs directly (Simpler and less error prone)
    [CalculateVersatData] naked memory allocation, add it to the accelerator locking mechanism
    Change accel reallocation to, instead of ordered, go to each unit, subtract original, then realloc and add offset to each unit. This completely removes order from the reallocation procedure and should make it easier to implement a RemoveUnit for allocated units, if needed

Embedded:

    Implement a perfect hashing scheme to accelerate simulation. GetInstanceByName

Hardware:

    Remove done/run/clk if not needed
    Implement shadow registers
    Delay units with same inputs and delay values should be shared.

Delay:

    To improve lantency calculations, should have a indication of which outputs delay depend on which input ports delay (Think how dual port ram has two independent ports)
        Each output should keep track of exactly which input port it depends upon, and use that information in delay calculation
    Since delay is simply "how many cycles until seeing valid data", I barely understand what is the use of DELAY_TYPE_SOURCE_DELAY and DELAY_TYPE_SINK_DELAY. Is it really just because of xreg?
        Also, is there any SinkAndSource instance that would implement DELAY_TYPE_SOURCE_DELAY? Because how would they know when they have valid data?
            Also also, when changing delays from instance based to port based, the concept of delay type will probably end. In that case, the different delay types would only serve as an "extra" to optimize away the delay wires to units that do not need them

    There are two types of delay: the normal delay is the amount of cycles until unit receives valid data on all ports, the delay for delay units indicates how many cycles to wait (this second delay should be general, could be used for the add-accum type of units)

Type:

    Rewrite the portions of the type system that you wanted too. Standerdize type info, type collapse into simpler types and automatic type casting (I have x, I want y)
    Implement object instrospection, printing of members and data automatically
    Redefine what a type is: a pointer or a array are completely different types, (Type(int) != Type(int*))
    Add multiple names to the same type (have an array of type info and hash names to type info)

Template Engine:

    Really need to simplify error if identifier not found.
    Add local variables, instead of everything global
    Take another pass at whitespace handling (Some whitespace is being consumed in blocks of text, care, might join together two things that are supposed to be seperated and do error)
    Need to find a way of reusing small pieces of template code. Both the top and individual accelerators reuse so much stuff that is similar, it's worth to take a look at it.
        Probably need to implement an include directive
        Also a defun directive, that takes a name and parameters that defines a piece of reusable template code
        And a call or insert directive, that takes a name plus arguments and simple outputs the template text (should be easy since we probably only need to store the block and execute it later)

Struct Parser:

    Rewrite the dumb stuff from the struct parser

Verilog Parser:

    Parse units interfaces to generate unitWrappers automatically and register units automatically (latency must be set directly by the user using a comment or attribute)

Flatten:

    Give every declaration a "level" variable. simple = 0, composite = max(instancesLevel) + 1, special = 0.
    Flatten is currently broken (might have been fixed, check it later?), only creating shallow instances. Useful for outputting graph, but not possible to simulate
        The fix is to create a function that copies and changes shallow instances and, in the end, it fixes memory allocation and instance pointers and initialization

Merge:

    Do merge of units in same "level". Iterate down while repeating

*/

/*

Known bugs:




*/





