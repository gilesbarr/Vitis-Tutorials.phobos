// The input data is read in one hunk of 512bit transfers.  IN_SIZE is the number of these transfers
#define IN_SIZE 16

// The sgoutd descriptors are read in sections (if we read in one go, size can no longer be a parameter)
// Lets read in clumps of 16x 512bits = 16x 64bytes = 1024bytes
// With a desciptor length of 32 bits, this means 1024 jumbo frames will be 4 sections
#define SGREADCLUMP 16
#define SGDESCWIDTH 32
#define SGDATAWIDTH 512
#define SGVECTORSIZE (SGDATAWIDTH / SGDESCWIDTH)

#define JUMBOSIZEBYTES 7168
#define DATAWIDTH 512
#define DATATYPE_SIZE 32
#define VECTOR_SIZE (DATAWIDTH / DATATYPE_SIZE) 

// TRIG_SIZE is the number of 512b words to output
#define TRIG_SIZE 128

