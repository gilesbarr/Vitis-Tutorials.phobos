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
//---    *sginoutt read A vector of size 2 containing offsets of where data is located
//---                    within *in and *outt relative to the starting pointe on the host for scatter-gather.
//---
//---    *sgoutd read  A vector of offsets for the jumbo frames to be written to in the 
//---                   *outd array.  The length of this vector is given by the size parameter
//---
//---    *in     read   An array where the configuration packet for the noise generator
//---                    is stored.  This is used in a very rudimentary way at the moment 
//---
//---    *outd   write  An array where the raw data jumbo packets are written.  The offsets of
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
//---    size    read   The number of output jumbo packets.
//---
//--------------------------------------------------------------------------------

#include <ap_int.h>

#include "kernelhost.h"

typedef ap_uint<DATAWIDTH> uint512_dt;  // DATAWIDTH and SGDATAWIDTH are the same, needs fixing if we change
typedef ap_uint<SGDESCWIDTH> sgdata_dt;
typedef ap_uint<DATATYPE_SIZE> din_type;
typedef ap_uint<DATATYPE_SIZE> dout_type;

//--------------------------------------------------------------------------------

extern "C" {
void vadd(const uint512_dt *sginoutt,
	  const uint512_dt *sgoutd,	
          const uint512_dt *in, 
          uint512_dt *outd,
          uint512_dt *outt,	  
          int size     // Size is usually 1024, the number of 7168byte jumbo packets (each is 8us on one link, so 8ms of data overall)           
          ) {
#pragma HLS INTERFACE m_axi port=sginoutt bundle=gmem num_write_outstanding=32 max_write_burst_length=64 num_read_outstanding=32 max_read_burst_length=64 offset=slave
#pragma HLS INTERFACE m_axi port=sgoutd   bundle=gmem num_write_outstanding=32 max_write_burst_length=64 num_read_outstanding=32 max_read_burst_length=64 offset=slave
#pragma HLS INTERFACE m_axi port=in       bundle=gmem num_write_outstanding=32 max_write_burst_length=64 num_read_outstanding=32 max_read_burst_length=64 offset=slave
#pragma HLS INTERFACE m_axi port=outd     bundle=gmem num_write_outstanding=32 max_write_burst_length=64 num_read_outstanding=32 max_read_burst_length=64 offset=slave
#pragma HLS INTERFACE m_axi port=outt     bundle=gmem num_write_outstanding=32 max_write_burst_length=64 num_read_outstanding=32 max_read_burst_length=64 offset=slave

  uint512_dt sginoutt_local;
  uint512_dt in_local[IN_SIZE];

  uint512_dt sgoutd_local[SGREADCLUMP];  
  uint512_dt outt_local[TRIG_SIZE]; 
  int size_in16 = size / (SGREADCLUMP*SGVECTORSIZE);     // Number of sections to read. e.g. 1024/16/16 = 4

  sgdata_dt descin;
  sgdata_dt desct;

  sginoutt_local = sginoutt[0];   // Read 512 bits from host, the first 64 are sgin and the next 64 are sgoutt
  descin = sginoutt_local.range(SGDESCWIDTH-1,0);
  desct = sginoutt_local.range(2*SGDESCWIDTH-1,SGDESCWIDTH);

  in_rd: for (int ia = 0; ia < IN_SIZE; ia++) {    // Read the complete block from host  16x 512b = 1024bytes
      in_local[descin + ia] = in[ia];  // PCIe inbound transfers
  } 

  for (int i = 0; i < size_in16*SGVECTORSIZE; i += SGREADCLUMP) {    // Split the buffer writing into sections (Iterates 4 times i=0,16,32,48)

    sgoutd_rd: for (int j = 0; j < SGREADCLUMP; j++) {  // Read the 256 =16x16) sgoutd descriptors for the current section
      sgoutd_local[j] = sgoutd[i + j];   // PCIe inbound transfers.  i+j is the offset in 512b steps in the host memory
    }

    sgclump1_loop: for (int jc = 0; jc < SGREADCLUMP; jc++) {   // Double loop (16x16) to traverse descriptor list and ...
      sgclump2_loop: for (int jd = 0; jd < SGVECTORSIZE; jd++) {  // ... unpack 16x inside the 512bits multi-descriptor
        sgdata_dt desc = sgoutd_local[jc].range(SGDESCWIDTH*(jd+1)-1,SGDESCWIDTH*jd);

        dataw1_loop: for (int iw = 0; iw < (8*JUMBOSIZEBYTES/DATATYPE_SIZE); iw += VECTOR_SIZE) {   // Loops 8*7168/32/16=112 times
          uint512_dt tmpOut = 0;
	  dout_type res;
	  dataw2_loop: for (int iv =0; iv < VECTOR_SIZE; iv++) {   // This loops over 512b as 16x 32b parts
            #pragma HLS UNROLL
            res = i + jc + in_local[iw ^ IN_SIZE];   // Think of something better for here
            tmpOut.range(DATATYPE_SIZE * (i + 1) - 1, i * DATATYPE_SIZE) = res;
	  }  // End of dataw2_loop: unrolled loop within the 512bits
          outd[desc + iw] = tmpOut;    // PCIe outbound transfer - aim is for all these loops to pipeline with ii=1
	} // End of dataw1_loop: over the jumbo packet
      } // End of sgclump2_loop: inner loop
    } // End of sgclump1_loop: outer descriptor loop
  }  // End of loop over clumps

  outt_wr: for (int iwt = 0; iwt<TRIG_SIZE; iwt++) {
    outt_local[iwt] = in_local[iwt & ((1<<IN_SIZE)-1)];    // iwt & 0xffff because IN_SIZE=16
    outt[desct + iwt] = outt_local[iwt];  // PCIe outbound transfer
  }

}  // end of function
}  // end extern "C"
