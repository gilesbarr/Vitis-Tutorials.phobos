/*
 * Copyright 2021 Xilinx, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ap_int.h>

// Size of 16 ticks x 256channels of 14bit packed ADCs = one jumbo frame (7168B = 114 x 512b) 
#define BUFFER_SIZE 114
#define DATAWIDTH 512
#define DATATYPE_SIZE 32
#define VECTOR_SIZE (DATAWIDTH / DATATYPE_SIZE)

#define SGDESC_SIZE 64 
#define SGBUFFER_SIZE 128 
#define SGVECTOR_SIZE (DATAWIDTH / SGDESC_SIZE)

typedef ap_uint<DATAWIDTH> uint512_dt;
typedef ap_uint<DATATYPE_SIZE> din_type;
typedef ap_uint<DATATYPE_SIZE + 1> dout_type;
typedef ap_uint<SGDESC_SIZE> sgin_type;

//-------------------------------------------------------------------------------
//---
//---  This version of the trial is intended to simulate an accelertator card
//---  processing incoming jumbo frames that have had ordering error-correction
//---  and outputting trigger primitives.  
//---
//---  In this 08.3 version of the trial, the *sgin vector is read as 512-bit PCIe
//---  transactions and then split into 8 64-bit objects.  In the other 08.4 trial,
//---  the transactions are 64-bit wide and read one object at a time.  The host
//---  code for both trials should be the same.
//--- 
//---  Parameters:
//---    *sgin   read   A vector of offsets indicating where data is located 
//---                    within *in1 and *out on the host for scatter-gather.  
//---                    The first offset is the offset within *out, the rest
//---                    are a list of jumbo packet locations within *in whose
//---                    length is indicated by the size parameter.
//--- 
//---    *in     read   An array containing the jumbo packets.  The offsets of
//---                    the jumbo packets are given in the list in *sgin.  This
//---                    array is intended to map to the whole of the DPDK membuff
//---                    area on the host.                    
//---
//---    *out    write  An area for the trigger primitive output to be placed for
//---                    all the packets processed from the kernel invocation.
//---                    Normally the size of the trigger primitive data is far
//---                    smaller than the input data, so we put it all in one
//---                    block and the host can re-sort it if necessary.  The 
//---                    start of the block being written to is given in the first
//---                    value in *sgin
//---
//---    size    read    The number of input jumbo packets.
//---
//--------------------------------------------------------------------------------

extern "C" {
void vadd(const uint512_dt *sgin, 
          const uint512_dt *in1, 
          uint512_dt *out,       
          int size              
          ) {
#pragma HLS INTERFACE m_axi port=sgin bundle=gmem num_write_outstanding=32 max_write_burst_length=64  num_read_outstanding=32 max_read_burst_length=64  offset=slave
#pragma HLS INTERFACE m_axi port=in1  bundle=gmem num_write_outstanding=32 max_write_burst_length=64  num_read_outstanding=32 max_read_burst_length=64  offset=slave
#pragma HLS INTERFACE m_axi port=out  bundle=gmem num_write_outstanding=32 max_write_burst_length=64  num_read_outstanding=32 max_read_burst_length=64  offset=slave

// sgin is a scatter-gather table. 

  uint512_dt sg_local[SGBUFFER_SIZE];
  int sgsize_in16 = size / SGVECTOR_SIZE;     // size is the total number of jumbo frames, sgsize_16 is the number of 512b words to read

  uint512_dt v1_local[BUFFER_SIZE];    
  uint512_dt result_local[BUFFER_SIZE]; 

  // Divide the jumbo packets into groups
  sg_group: for (int isg = 0; isg < sgsize_in16; isg += SGBUFFER_SIZE) {  

      // Read a group of addresses of jumbo packets from host
      for (int jsg = 0; jsg < SGBUFFER_SIZE; jsg++) {
         sg_local[jsg] = sgin[isg + jsg];
      }

      for (int jsg = 0; jsg < SGBUFFER_SIZE; jsg++) {    // Step through the list of 512b multi-group descriptors
          uint512_dt tmpSGV = sg_local[jsg];

          for (int ksg = 0; ksg < SGVECTOR_SIZE; ksg++) {
              sgin_type jumbo = tmpSGV.range(SGDESC_SIZE * (ksg + 1) - 1, ksg * SGDESC_SIZE);
              din_type val3 = ((isg + jsg) * SGDESC_SIZE + ksg) & (DATATYPE_SIZE - 1);

              // Read the data from the jumbo frame
              v1_rd: for (int j = 0; j < BUFFER_SIZE; j++) {
                  v1_local[j] = in1[jumbo + j];
              }	

              // Process the data from the jumbo rame
              v2_rd_add: for (int j = 0; j < BUFFER_SIZE; j++) {
                  uint512_dt tmpV1 = v1_local[j];
                  uint512_dt tmpOut = 0;
                  din_type val1;
                  din_type val2 = val3 ^ j;    // This is the XOR operator
                  dout_type res;

                  v2_parallel_add: for (int i = 0; i < VECTOR_SIZE; i++) {
                      #pragma HLS UNROLL
                      val1 = tmpV1.range(DATATYPE_SIZE * (i + 1) - 1, i * DATATYPE_SIZE);
                      res = val1 | val2;       // This is the OR operator
                      tmpOut.range(DATATYPE_SIZE * (i + 1) - 1, i * DATATYPE_SIZE) = res; 
                  }  // end of v2_parallel_add
                  result_local[j] = tmpOut;
              }  // end of v2_rd_add

              out_write: for (int j = 0; j < BUFFER_SIZE; j++) {
                  out[ksg + j] = result_local[j];
              }  // end of out_write
          }  // End of for(ksg) loop over jumbo frames in 512-vector
      }  // end of for(jsg) loop over jumbo frame 512-vectors in group
   }  // End of sg_group loop of jumbo frames
} // End of function
} // End extern "C"
