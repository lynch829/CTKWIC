#ifndef read_raw_file_sensation64frame_h
#define read_raw_file_sensation64frame_h

#include <stdint.h>

// Substructures
struct IrsInfoIrsHeader{
    //16 bytes
    int Status;
    float TubeAngle;
    int AcqTimestamp;
    int TablePosition;
    //16 bytes
    float RefDetector;
    int Error;
    float Maximum;
    int IntegrationTime;
    //16 bytes
    int AngleStatus;
    uint16_t SegmentNum;
    uint16_t ZCtrlSector;
    int Dose_Avg360_mA;
    int Dose_ActRdg_mA;
    //16 bytes
    char reserved[16];
};

struct IrsInfoDasHeader{
    char h4[112];
};

struct IrsInfoDasFooter{
    char h5[16];
};

struct IrsInfoSliceFooter{
    unsigned int ReadingNo;
    char reserved[10];
    short Exponent;
};

struct Sensation64Frame{
    // Header frame data
    struct IrsInfoIrsHeader IrsHeader;
    struct IrsInfoDasHeader DasHeader;
    struct IrsInfoDasFooter DasFooter;
    // Pointer to array of SliceFooter data on the heap
    struct IrsInfoSliceFooter * SliceFooter;

    // Pointer to calloc'ed data on the heap
    float * frame_data;    
};
    
#endif
