//
//  definitionasframe.h
//  read_raw_file
//
//  Created by Stefano Young on 8/14/14.
//  Copyright (c) 2014 Radiological Sciences. All rights reserved.
//

#include <stdint.h>

#ifndef definitionasframe_h
#define definitionasframe_h

// SciRawIrsInfoDet
struct	SciRawIrsInfoDet{
    int32_t Error;					// IRS marker for defective slices
    uint16_t NoOfChannels;			// no. of channels
    uint16_t NoOfScatterMonChannels;	// no. of scatter monitoring channels
    float TubeAngle;				// tube angle     [deg] /* for DetB corrected with TubeBOffsetAngle */
    int32_t TablePosition;			// table position [um]  /* for DetB corrected with TubeBZOffset     */
    float RefDetector;			// dose monitor (FPA decoded + log.)
    float Maximum;				// maximum channel value of all slices
    uint16_t TubeCurrent_ActRdg;	// applied mA for actual reading
    uint16_t TubeCurrent_Avg360;	// applied mA over last 360 degrees
    float Minimum;				// minimum channel value of all slices
};

struct status{
    unsigned	Model					: 8;	// model id [SCI_RAW_MODEL_...]
    unsigned	DataType				: 8;	// type of data [SciTypes_e]
    unsigned	ValidDetector			: 1;	// reading contains valid		 data
    unsigned	DefectiveReading		: 1;	//    "        "    defective	  "
    unsigned	InterpolatedReading		: 1;	//    "        "    interpolated  "
    unsigned	ValidDetector2			: 1;	// reading contains valid		 data
    unsigned	DefectiveReading2		: 1;	//    "        "    defective	  "
    unsigned	InterpolatedReading2	: 1;	//    "        "    interpolated  "
    unsigned							: 1;
    unsigned	DualDetector			: 1;	// 0=single detector data, 1=dual detector data
    unsigned	LastInScan				: 1;	// last reading in scan
    unsigned	FirstInScan				: 1;	// first reading in scan
    unsigned							: 6;	// reserved (must be 0)            
};					// reading status

struct noof{
    unsigned	Slices					: 12;	// no. of slices in reading
    unsigned    SlicesMon                               : 4;   // no. of scatter monitor slices
    unsigned	PhiFFS					: 4;	// no. of Phi FFS
    unsigned	ZFFS					: 4;	// no. of Z FFS
    unsigned	Integrators				: 4;	// no. of integrators            
};

struct SciRawIrsInfoBlock{
    struct status Status;
    struct noof NoOf;
    int32_t ScanNumber;			// scan number
    int32_t ReadingNumber;			// reading number
    int32_t AcqTimestamp;			// time stamp of data acquisition [10us]
    uint16_t IntegrationTime;		// integration time [us]
    int16_t GantryTilt;			// gantry tilt [0.1 deg]
    int32_t VerticalTablePos;		// vertical table position [um]
    struct SciRawIrsInfoDet A;
    struct SciRawIrsInfoDet B;
    int32_t ScanBeginTime;  // TODO: this is just a placeholder
    int16_t pad1;
    uint16_t TubePosition;			// tube position
    int8_t pad[28];
};

struct DefinitionASFrame{
    struct SciRawIrsInfoBlock InfoBlock;
    float * frame_data;
};

#endif
