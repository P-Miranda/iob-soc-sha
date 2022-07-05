extern "C"{
#include <stdio.h>
#include "system.h"
#include "periphs.h"
#include "iob-uart.h"
#include "printf.h"

#include "iob-timer.h"
#include "iob-eth.h"

}

#include "versat.hpp"

#include "unitWrapper.hpp"
#include "unitVerilogWrappers.hpp"

#define HASH_SIZE (256/8)

// Pointer to DDR_MEM
#ifdef PC
    char ddr_mem[100000] = {0};
#else
    char *ddr_mem = (char*) EXTRA_BASE;
#endif

// GLOBALS
Accelerator* accel;
bool initVersat = false;

static int* readMemory;

static uint initialStateValues[] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
static uint kConstants0[] = {0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174};
static uint kConstants1[] = {0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967};
static uint kConstants2[] = {0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070};
static uint kConstants3[] = {0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

static uint* kConstants[4] = {kConstants0,kConstants1,kConstants2,kConstants3};
/* read integer value
 * return number of bytes read */
int get_int(char* ptr, unsigned int *i_val){
    /* check for valid ptr */
    if(ptr == NULL){
        printf("get_int: invalid pointer\n");
        return -1;
    }
    /* read 1 byte at a time
     * write to int */
    *i_val = (unsigned char) ptr[3];
    *i_val <<= 8;
    *i_val += (unsigned char) ptr[2];
    *i_val <<= 8;
    *i_val += (unsigned char) ptr[1];
    *i_val <<= 8;
    *i_val += (unsigned char) ptr[0];
    return sizeof(int);
}

/* get pointer to message and increment pointer */
/* get pointer to message
 * return size of message */
int get_msg(char *ptr, uint8_t **msg_ptr, int size){
    /* check for valid ptr */
    if(ptr == NULL){
        printf("get_msg: invalid pointer\n");
        return -1;
    }
    /* update message pointer */
    *msg_ptr = (uint8_t*) ptr;
    return sizeof(uint8_t)*size;
}

/* copy memory from pointer to another */
void mem_copy(uint8_t *src_buf, char *dst_buf, int size){
    if(src_buf == NULL || dst_buf == NULL){
        printf("mem_copy: invalid pointer\n");
        return;
    }
    int i = 0;
    while(i<size){
        dst_buf[i] = src_buf[i];
        i++;
    }
    return;
}

/* copy mesage to pointer, update pointer to after message
 * returns number of chars written
 */
int save_msg(char *ptr, uint8_t* msg, int size){
    if(ptr == NULL || msg == NULL || size < 0 ){
        printf("save_msg: invalid inputs\n");
        return -1;
    }

    // copy message to pointer
    mem_copy(msg, ptr, size); 
    return sizeof(uint8_t)*size;
}

void InstantiateSHA(Versat* versat){
    FUInstance* inst = nullptr;
    FUDeclaration* type = GetTypeByName(versat,MakeSizedString("SHA"));
    accel = CreateAccelerator(versat);
    inst = CreateNamedFUInstance(accel,type,MAKE_SIZED_STRING("SHA"));

    OutputUnitInfo(inst);

    FUInstance* read = GetInstanceByName(accel,"SHA","MemRead");
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

    for(int i = 0; i < 4; i++){
        FUInstance* mem = GetInstanceByName(accel,"SHA","cMem%d",i);

        mem->config[0] = 1;
        mem->config[1] = 16;
        mem->config[2] = 16;
        mem->config[5] = 1;

        for(int ii = 0; ii < 16; ii++){
            VersatUnitWrite(mem,ii,kConstants[i][ii]);
        }
    }

    CalculateDelay(versat,accel);
    SetDelayRecursive(inst,0);
}

static uint load_bigendian_32(const uint8_t *x) {
     return (uint)(x[3]) | (((uint)(x[2])) << 8) |
                (((uint)(x[1])) << 16) | (((uint)(x[0])) << 24);
}

static void store_bigendian_32(uint8_t *x, uint64_t u) {
    x[3] = (uint8_t) u;
    u >>= 8;
    x[2] = (uint8_t) u;
    u >>= 8;
    x[1] = (uint8_t) u;
    u >>= 8;
    x[0] = (uint8_t) u;
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

int main(int argc, const char* argv[])
{
  int INPUT_FILE_SIZE = 4096;
  uint8_t digest[256];
  unsigned int num_msgs = 0;
  unsigned int msg_len = 0;
  int i = 0;

  int din_size = 0, din_ptr = 0;
  // input file points to ddr_mem start
  char *din_fp = (char*) ddr_mem;

  int dout_size = 0, dout_ptr = 0;
  char *dout_fp = NULL;

  //init uart
  uart_init(UART_BASE,FREQ/BAUD);   

  //init timer
  timer_init(TIMER_BASE);

  //init ethernet
  eth_init(ETHERNET_BASE);

  //instantiate versat 
  Versat versatInst = {};
  Versat* versat = &versatInst;
  InitVersat(versat,VERSAT_BASE,1); 

  // Sha specific units
  // Need to RegisterFU, can ignore return value
  RegisterUnitF(versat);
  RegisterUnitM(versat);

  ParseVersatSpecification(versat,"testVersatSpecification.txt");

  InstantiateSHA(versat);
  printf("After Instantiation SHA\n");

#ifdef SIM
  //Receive input data from uart
  din_size = uart_recvfile("sim_in.bin", &din_fp);
#else
  //Receive input data from ethernet
  din_size = eth_rcv_variable_file(din_fp);
#endif
  printf("ETHERNET: Received file with %d bytes\n", din_size);

  // Calculate output size and allocate output memory
  din_ptr += get_int(din_fp + din_ptr, &num_msgs);
  dout_size = num_msgs*HASH_SIZE;
  // output file starts after input file
  dout_fp = din_fp + din_size;

  uint8_t *msg = NULL;

  //Message test loop
  for(i=0; i< num_msgs; i++){
    // Parse message and length
    din_ptr += get_int(din_fp + din_ptr, &msg_len);
    din_ptr += get_msg(&(din_fp[din_ptr]), &msg, ((msg_len) ? msg_len : 1) );

    versat_sha256(digest,msg,msg_len);

    //save to memory
    dout_ptr += save_msg(&(dout_fp[dout_ptr]), digest, HASH_SIZE);
  }

#ifdef SIM
  // send message digests via uart
  uart_sendfile("soc-out.bin", dout_size, dout_fp); 
#else
  // send message digests via ethernet
  eth_send_variable_file(dout_fp, dout_size);
#endif
  printf("ETHERNET: Sent file with %d bytes\n", dout_size);

  uart_finish();
}
