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

// This version reads the descriptors 64 bits at a time, so does not optimise the 512bit transfers explicitly (but has less looping)
// However it may be problematic that we have 64-bit and 512bit reads from the same axi bundle 'gmem'

#define SGDESC_SIZE 64 
typedef ap_uint<SGDESC_SIZE> uint64_dt;
#define SGBUFFER_SIZE 1024 
#define SGVECTOR_SIZE 1

typedef ap_uint<DATAWIDTH> uint512_dt;
typedef ap_uint<DATATYPE_SIZE> din_type;
typedef ap_uint<DATATYPE_SIZE + 1> dout_type;
typedef ap_uint<SGDESC_SIZE> sgin_type;

extern "C" {
void vadd(const sgin_type *sgin, 
          const uint512_dt *in1, 
          uint512_dt *out,       
          int size              
          ) {
#pragma HLS INTERFACE m_axi port=sgin bundle=gmem num_write_outstanding=32 max_write_burst_length=64  num_read_outstanding=32 max_read_burst_length=64  offset=slave
#pragma HLS INTERFACE m_axi port=in1  bundle=gmem num_write_outstanding=32 max_write_burst_length=64  num_read_outstanding=32 max_read_burst_length=64  offset=slave
#pragma HLS INTERFACE m_axi port=out  bundle=gmem num_write_outstanding=32 max_write_burst_length=64  num_read_outstanding=32 max_read_burst_length=64  offset=slave

// sgin is the scatter-gather table. 

  sgin_type sg_local[SGBUFFER_SIZE];
  int sgsize_in16 = size;     // size is the total number of jumbo frames, sgsize_16 is the number of words to read (same in this case)

  uint512_dt v1_local[BUFFER_SIZE];    
  uint512_dt result_local[BUFFER_SIZE]; 

  // Divide the jumbo packets into groups
  sg_group: for (int isg = 0; isg < sgsize_in16; isg += SGBUFFER_SIZE) {  

      // Read a group of addresses of jumbo packets from host
      for (int jsg = 0; jsg < SGBUFFER_SIZE; jsg++) {
         sg_local[jsg] = sgin[isg + jsg];
      }

      for (int jsg = 0; jsg < SGBUFFER_SIZE; jsg++) {    // Step through the list of 512b multi-group descriptors
        sgin_type jumbo = sg_local[jsg];

              // Read the data from the jumbo frame
              v1_rd: for (int j = 0; j < BUFFER_SIZE; j++) {
                  v1_local[j] = in1[jumbo + j];
              }	

              // Process the data from the jumbo rame
              v2_rd_add: for (int j = 0; j < BUFFER_SIZE; j++) {
                  uint512_dt tmpV1 = v1_local[j];
                  uint512_dt tmpV2 = v1_local[j];      // was in2['jumbo' + j];
                  uint512_dt tmpOut = 0;
                  din_type val1, val2;
                  dout_type res;

                  v2_parallel_add: for (int i = 0; i < VECTOR_SIZE; i++) {
                      #pragma HLS UNROLL
                      val1 = tmpV1.range(DATATYPE_SIZE * (i + 1) - 1, i * DATATYPE_SIZE);
                      val2 = tmpV2.range(DATATYPE_SIZE * (i + 1) - 1, i * DATATYPE_SIZE);
                      res = val1 + val2; 
                      tmpOut.range(DATATYPE_SIZE * (i + 1) - 1, i * DATATYPE_SIZE) = res; 
                  }  // end of v2_parallel_add
                  result_local[j] = tmpOut;
              }  // end of v2_rd_add

              out_write: for (int j = 0; j < BUFFER_SIZE; j++) {
                  out[i + j] = result_local[j];
              }  // end of out_write
      }  // end of for(jsg) loop over jumbo frame 512-vectors in group
   }  // End of sg_group loop of jumbo frames
} // End of function
} // End extern "C"
