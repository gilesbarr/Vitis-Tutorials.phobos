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

#define BUFFER_SIZE 128
#define DATAWIDTH 512
#define DATATYPE_SIZE 32
#define VECTOR_SIZE (DATAWIDTH / DATATYPE_SIZE) 

typedef ap_uint<DATAWIDTH> uint512_dt;
typedef ap_uint<DATATYPE_SIZE> din_type;
typedef ap_uint<DATATYPE_SIZE + 1> dout_type;

//-------------------------------------------------------------------------------
//---
//---  This version of the trial is intended to simulate a U50 as DUNE intend to
//---  use it for jumbo frame ingress and processing if trigger primitives.  The 
//---  incoming data in this simulation are generated internally when the kernel is
//---  activated, which is not the way this will work in reality where data will be 
//---  received from the UDP interface.  This has as yet unresolved issues for when
//---  the kernel is not running.
//---
//---  Parameters:
//---    *sgin   read   A vector of offsets indicating where data is located
//---                    within *outt and *outd on the host for scatter-gather.
//---                    The first offset is the offset within *outt, the rest
//---                    are a list of jumbo packet locations within *outd.  The
//---                    length of this list is indicated by the size parameter.
//---
//---    *outd   write   An array where the jumbo packets are written.  The offsets of
//---                    the jumbo packets are given in the list in *sgin.  This
//---                    array is intended to map to the whole of the DPDK membuff
//---                    area on the host.
//---
//---    *outt   write  An area for the trigger primitive output to be placed for
//---                    all the packets processed from the kernel invocation.
//---                    Normally the size of the trigger primitive data is far
//---                    smaller than the raw data.  The start of the block being 
//---                    written to is given in the first value in *sgin
//---
//---    size    read    The number of input jumbo packets.
//---
//--------------------------------------------------------------------------------

extern "C" {
void vadd(const uint512_dt *in1, 
          const uint512_dt *in2, 
          uint512_dt *out,       
          int size              
          ) {
#pragma HLS INTERFACE m_axi port=in1 bundle=gmem num_write_outstanding=32 max_write_burst_length=64  num_read_outstanding=32 max_read_burst_length=64  offset=slave
#pragma HLS INTERFACE m_axi port=in2 bundle=gmem num_write_outstanding=32 max_write_burst_length=64  num_read_outstanding=32 max_read_burst_length=64  offset=slave
#pragma HLS INTERFACE m_axi port=out bundle=gmem num_write_outstanding=32 max_write_burst_length=64 num_read_outstanding=32 max_read_burst_length=64 offset=slave

  uint512_dt v1_local[BUFFER_SIZE];    
  uint512_dt result_local[BUFFER_SIZE]; 

  int size_in16 = size / VECTOR_SIZE ;

  for (int i = 0; i < size_in16; i += BUFFER_SIZE) {

  v1_rd: for (int j = 0; j < BUFFER_SIZE; j++) {
      v1_local[j] = in1[i + j];
    }

  v2_rd_add: for (int j = 0; j < BUFFER_SIZE; j++) {
      uint512_dt tmpV1 = v1_local[j];
      uint512_dt tmpV2 = in2[i + j];
      uint512_dt tmpOut = 0;
      din_type val1, val2;
      dout_type res;

    v2_parallel_add: for (int i = 0; i < VECTOR_SIZE; i++) {
        #pragma HLS UNROLL
        val1 = tmpV1.range(DATATYPE_SIZE * (i + 1) - 1, i * DATATYPE_SIZE);
        val2 = tmpV2.range(DATATYPE_SIZE * (i + 1) - 1, i * DATATYPE_SIZE);
        res = val1 + val2; 
        tmpOut.range(DATATYPE_SIZE * (i + 1) - 1, i * DATATYPE_SIZE) = res; 
      }
      result_local[j] = tmpOut;
    }

  out_write: for (int j = 0; j < BUFFER_SIZE; j++) {
      out[i + j] = result_local[j];
    }
  }
}
}
