//
//  read_raw_file.h
//  read_raw_file
//
//  Created by Stefano Young on 8/14/14.
//  Copyright (c) 2014 Radiological Sciences. All rights reserved.


#include "sensation64frame.h"
#include <stdio.h>
#include <stdlib.h>

#ifndef read_raw_file_h
#define read_raw_file_h

//PTR Readers
void ReadPTRFrame(FILE* fp,int frame_index,int num_channels,int num_slices,float *f);
float ReadPTRTubeAngle(FILE* fp, int frame_index,int num_channels,int num_slices);
long ReadPTRTablePosition(FILE* fp, int frame_index,int num_channels,int num_slices);

//CTD Readers
void ReadCTDFrame(FILE* fp,int frame_index,int num_channels,int num_slices,float *f);
float ReadCTDTubeAngle(FILE* fp, int frame_index,int num_channels,int num_slices);
long ReadCTDTablePosition(FILE* fp, int frame_index,int num_channels,int num_slices);

// IMA files (Can be either PTR or CTD)
void ReadIMAFrame(FILE* fp, int frame_index, int num_channels, int num_slices, float *f,int raw_data_type,size_t offset);
float ReadIMATubeAngle(FILE* fp, int frame_index,int num_channels,int num_slices,int raw_data_type,size_t offset);
long ReadIMATablePosition(FILE* fp, int frame_index,int num_channels,int num_slices,int raw_data_type,size_t offset);

// Binary files
void ReadBinaryFrame(FILE* fp,int frame_index,int num_channels,int num_slices,float *f);

struct Sensation64Frame  read_CTD_frame(FILE* fp, int frame_index,int num_channels,int num_slices,int frame_flag,size_t offset);
struct DefinitionASFrame read_PTR_frame(FILE* fp, int frame_index,int num_channels,int num_slices,int frame_flag,size_t offset);

//IMA Readers


#endif
