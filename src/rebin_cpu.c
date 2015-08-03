#define pi 3.1415926535897f

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <complex.h>
#include <fftw3.h>

#include <interp.h>
#include <recon_structs.h>
#include <rebin_cpu.h>

void copy_sheet(float * sheetptr, int row, struct recon_metadata *mr, struct ct_geom cg);
void load_filter(float * f_array,struct recon_metadata * mr);

float angle(float x1,float x2,float y1,float y2){
    return asin((x1*y2-x2*y1)/(sqrt(x1*x1+x2*x2)*sqrt(y1*y1+y2*y2)));
}

float beta_rk(float da,float dr,float channel,int os_flag, struct ct_geom cg){
    float b0=(channel-pow(2.0f,os_flag)*cg.central_channel)*(cg.fan_angle_increment/pow(2.0f,os_flag));
    return angle(-(cg.r_f+dr),-(da),-(cg.src_to_det*cos(b0)+dr),-(cg.src_to_det*sin(b0)+da));
}

float d_alpha_r(float da,float dr,struct ct_geom cg){
    return angle(cg.r_f,0,cg.r_f+dr,da);
}

float r_fr(float da, float dr,struct ct_geom cg){
    return sqrt((cg.r_f+dr)*(cg.r_f+dr)+da*da);
}

float get_beta_idx(float beta,float * beta_lookup,int n_elements){
    int idx_low=0;

    while (beta>beta_lookup[idx_low]&&idx_low<(n_elements-1)){
    	idx_low++;
    }

    if (idx_low==0)
	idx_low++; 
    
    return (float)idx_low-1.0f+(beta-beta_lookup[idx_low-1])/(beta_lookup[idx_low]-beta_lookup[idx_low-1]);
}


void filter_cpu(float * row, float * filter, int N);

void rebin_nffs_cpu(struct recon_metadata *mr);
void rebin_pffs_cpu(struct recon_metadata *mr);
void rebin_zffs_cpu(struct recon_metadata *mr);
void rebin_affs_cpu(struct recon_metadata *mr);

int rebin_filter_cpu(struct recon_metadata * mr){

    switch (mr->ri.n_ffs){
    case 1:{
	rebin_nffs_cpu(mr);
	break;}
    case 2:{
	if (mr->rp.z_ffs==1)
	    rebin_zffs_cpu(mr);
	else
	    rebin_pffs_cpu(mr);
	break;}
    case 4:{
	rebin_affs_cpu(mr);
	break;}
    }
    
    return 0;
}

void rebin_nffs_cpu(struct recon_metadata *mr){
    const struct ct_geom cg=mr->cg;
    const struct recon_params rp=mr->rp;

    float * h_output=(float*)calloc(cg.n_channels_oversampled*cg.n_rows*mr->ri.n_proj_pull/mr->ri.n_ffs,sizeof(float));

    // We want to rebin the 1/4 or 1/8 detector offset away
    float kwic_central_channel=((float)cg.n_channels_oversampled-1.0f)/2.0f;
    
    // Main loop
    int n_proj=mr->ri.n_proj_pull/mr->ri.n_ffs;
    struct array_dims d;
    d.idx1=cg.n_channels;
    d.idx2=cg.n_rows;
    d.idx3=n_proj;
    
    for (int channel=0;channel<cg.n_channels_oversampled;channel++){
	const float beta=asin(((float)channel-kwic_central_channel)*(rp.recon_fov/(cg.n_channels_oversampled-1))/cg.r_f);
	float beta_idx=beta/cg.fan_angle_increment+cg.central_channel;
	for (int proj=0;proj<n_proj;proj++){
	    float alpha_idx=(float)proj-beta*cg.n_proj_turn/(2.0f*pi);
	    for (int row=0;row<cg.n_rows;row++){
		int out_idx=cg.n_channels_oversampled*cg.n_rows*proj+cg.n_channels_oversampled*row+channel;
		h_output[out_idx]=interp3(mr->ctd.raw,d,beta_idx,row,alpha_idx);
	    }
	}
    }

    //Copy data into our mr structure, skipping initial truncated projections
    size_t offset=cg.add_projections;
    for (int i=0;i<cg.n_channels_oversampled;i++){
	for (int j=0;j<cg.n_rows;j++){
	    for (int k=0;k<(mr->ri.n_proj_pull/mr->ri.n_ffs-2*cg.add_projections);k++){
		mr->ctd.rebin[k*cg.n_channels_oversampled*cg.n_rows+j*cg.n_channels_oversampled+i]=h_output[(k+offset)*cg.n_channels_oversampled*cg.n_rows+j*cg.n_channels_oversampled+i];
	    }
	}
    }

    
//    printf("Filtering...\n");
//    
//    // Load and run filter
//    float * h_filter=(float*)calloc(2*cg.n_channels_oversampled,sizeof(float));
//    load_filter(h_filter,mr);
//
//    for (int i=0;i<(n_proj-2*cg.add_projections);i++){
//	for (int j=0;j<cg.n_rows;j++){
//	    int row_start_idx=i*cg.n_channels_oversampled*cg.n_rows+cg.n_channels_oversampled*j;
//	    filter_cpu(&mr->ctd.rebin[row_start_idx],h_filter,cg.n_channels_oversampled);
//	}
//    }
    
    // Check "testing" flag, write rebin to disk if set
    if (mr->flags.testing){
	char fullpath[4096+255];
	strcpy(fullpath,mr->homedir);
	strcat(fullpath,"/Desktop/rebin.ct_test");
	FILE * outfile=fopen(fullpath,"w");
	fwrite(mr->ctd.rebin,sizeof(float),cg.n_channels_oversampled*cg.n_rows*(mr->ri.n_proj_pull-2*cg.add_projections_ffs)/mr->ri.n_ffs,outfile);
	fclose(outfile);
    }

    free(h_output);
//  free(h_filter);
}

void rebin_pffs_cpu(struct recon_metadata *mr){

    // Set up some constants
    struct ct_geom cg=mr->cg;
    struct recon_params rp=mr->rp;
    struct recon_info ri=mr->ri;
    const double da=cg.src_to_det*cg.r_f*cg.fan_angle_increment/(4.0f*(cg.src_to_det-cg.r_f));
    int n_proj=mr->ri.n_proj_pull/mr->ri.n_ffs;

    // We want to rebin the 1/4 or 1/8 detector offset away
    float kwic_central_channel=((float)cg.n_channels_oversampled-1.0f)/2.0f;

    // Allocate raw data arrays and intermediate output array
    float * raw_1;
    raw_1=(float*)malloc(cg.n_channels*cg.n_rows*n_proj*sizeof(float));
    float * raw_2;
    raw_2=(float*)malloc(cg.n_channels*cg.n_rows*n_proj*sizeof(float));
    
    float * rebin_t;
    rebin_t=(float*)malloc(cg.n_channels_oversampled*cg.n_rows*n_proj*sizeof(float));

    // Split raw data by focal spot
    for (int i=0;i<n_proj;i++){
	for (int j=0;j<cg.n_rows;j++){
	    for (int k=0;k<cg.n_channels;k++){
		int out_idx=cg.n_channels*cg.n_rows*i+cg.n_channels*j+k;
		int in_idx_ffs1=cg.n_channels*cg.n_rows*(2*i)+cg.n_channels*j+k;
		int in_idx_ffs2=cg.n_channels*cg.n_rows*(2*i+1)+cg.n_channels*j+k;

		raw_1[out_idx]=mr->ctd.raw[in_idx_ffs1];
		raw_2[out_idx]=mr->ctd.raw[in_idx_ffs2];
	    }
	}
    }

    struct array_dims dim;
    dim.idx1=cg.n_channels;
    dim.idx2=cg.n_rows;
    dim.idx3=n_proj;

    // Allocate the lookup table we'll use in the channel rebinning
    float * beta_lookup;
    beta_lookup=(float*)malloc(cg.n_channels_oversampled*sizeof(float));

    // Predetermine our betas so we can reuse throughout computation
    float * betas_1;
    float * betas_2;
    betas_1=(float *)malloc(sizeof(float)*cg.n_channels);
    betas_2=(float *)malloc(sizeof(float)*cg.n_channels);
    for (int i=0;i<cg.n_channels;i++){
	betas_1[i]=beta_rk(da,0,i,0,cg);
	betas_2[i]=beta_rk(-da,0,i,0,cg);
	beta_lookup[2*i]=betas_1[i];
	beta_lookup[2*i+1]=betas_2[i];	
    }
    
    // Rebin over angles
    for (int proj=0;proj<n_proj;proj++){
	printf("%d/%d\n",proj,n_proj);
	for (int row=0;row<cg.n_rows;row++){
	    for (int channel=0;channel<cg.n_channels;channel++){
		
		int out_idx_1=proj*cg.n_channels_oversampled*cg.n_rows+row*cg.n_channels_oversampled+2*channel;
		int out_idx_2=proj*cg.n_channels_oversampled*cg.n_rows+row*cg.n_channels_oversampled+2*channel+1;

		// +da
		float beta_1= betas_1[channel];
		float alpha_idx_1=ri.n_ffs*(proj)-beta_1*cg.n_proj_ffs/(2.0f*pi)-d_alpha_r(da,0,cg)*cg.n_proj_ffs/(2.0f*pi);
		
		// -da
		float beta_2 = betas_2[channel];
		float alpha_idx_2=ri.n_ffs*(proj)-beta_2*cg.n_proj_ffs/(2.0f*pi)-d_alpha_r(-da,0,cg)*cg.n_proj_ffs/(2.0f*pi);

		// Rescale alpha indices to properly index the raw arrays as 0, 1, 2, 3, ...
		alpha_idx_1=alpha_idx_1/2.0f; // raw_1 contains alpha projections 0, 2, 4, 6, ...
		alpha_idx_2=(alpha_idx_2-1.0f)/2.0f; // raw_2 contains projections 1, 3, 5, 7, ...
		
		rebin_t[out_idx_1]=interp3(raw_1,dim,channel,row,alpha_idx_1);
		rebin_t[out_idx_2]=interp3(raw_2,dim,channel,row,alpha_idx_2);
	    }
	}
    }

    FILE * o;
    o=fopen("/home/john/Desktop/beta_lookup_pffs.txt","w");
    fwrite(beta_lookup,sizeof(float),cg.n_channels_oversampled,o);
    fclose(o);
    
    // Free any arrays we no longer need, allocate final output array
    free(raw_1);
    free(raw_2);
    
    float * h_output;
    h_output=(float*)malloc(cg.n_channels_oversampled*cg.n_rows*n_proj*sizeof(float));

    // Update the interpolation array dimensions since new array has twice as many channels
    dim.idx1*=2;

    // Predetermine our betas and beta indices once to minimize lookup searches
    float * betas;
    float * beta_indices;
    betas=(float*)malloc(sizeof(float)*cg.n_channels_oversampled);
    beta_indices=(float*)malloc(sizeof(float)*cg.n_channels_oversampled);
    for (int i=0; i<cg.n_channels_oversampled;i++){
	betas[i]=asin(((float)i-kwic_central_channel)*(rp.recon_fov/(cg.n_channels_oversampled-1))/cg.r_f);
	beta_indices[i]=get_beta_idx(betas[i],beta_lookup,cg.n_channels_oversampled);
    }
    
    // Rebin channels
    for (int proj=0;proj<n_proj;proj++){
	printf("%d/%d\n",proj,n_proj);
	for (int row=0;row<cg.n_rows;row++){
	    for (int channel=0;channel<cg.n_channels_oversampled;channel++){

		//float beta  = asin((channel-2*cg.central_channel)*(cg.fan_angle_increment/2));
		//const float beta=asin(((float)channel-kwic_central_channel)*(rp.recon_fov/(cg.n_channels_oversampled-1))/cg.r_f);
		float beta=betas[channel];

		//float beta_idx=get_beta_idx(beta,beta_lookup,cg.n_channels_oversampled);
		float beta_idx=beta_indices[channel];
		
		int out_idx=cg.n_channels_oversampled*cg.n_rows*proj+cg.n_channels_oversampled*row+channel;
		h_output[out_idx]=interp3(rebin_t,dim,beta_idx,row,proj);
	    }
	}
    }

    free(rebin_t);
    free(betas);
    free(beta_indices);
    
    //Copy data into our mr structure, skipping initial truncated projections
    size_t offset=cg.add_projections;
    for (int i=0;i<cg.n_channels_oversampled;i++){
	for (int j=0;j<cg.n_rows;j++){
	    for (int k=0;k<(mr->ri.n_proj_pull/mr->ri.n_ffs-2*cg.add_projections);k++){
		mr->ctd.rebin[k*cg.n_channels_oversampled*cg.n_rows+j*cg.n_channels_oversampled+i]=h_output[(k+offset)*cg.n_channels_oversampled*cg.n_rows+j*cg.n_channels_oversampled+i];
	    }
	}
    }

//    printf("Filtering...\n");
//    
//    // Load and run filter
//    float * h_filter=(float*)calloc(2*cg.n_channels_oversampled,sizeof(float));
//    load_filter(h_filter,mr);
//
//    for (int i=0;i<(n_proj-2*cg.add_projections);i++){
//	for (int j=0;j<cg.n_rows;j++){
//	    int row_start_idx=i*cg.n_channels_oversampled*cg.n_rows+cg.n_channels_oversampled*j;
//	    filter_cpu(&mr->ctd.rebin[row_start_idx],h_filter,cg.n_channels_oversampled);
//	}
//    }
    
    // Check "testing" flag, write rebin to disk if set
    if (mr->flags.testing){
	char fullpath[4096+255];
	strcpy(fullpath,mr->homedir);
	strcat(fullpath,"/Desktop/rebin.ct_test");
	FILE * outfile=fopen(fullpath,"w");
	fwrite(mr->ctd.rebin,sizeof(float),cg.n_channels_oversampled*cg.n_rows*(mr->ri.n_proj_pull-2*cg.add_projections_ffs)/mr->ri.n_ffs,outfile);
	fclose(outfile);
    }

    free(h_output);
    //free(h_filter);

}

void rebin_zffs_cpu(struct recon_metadata *mr){

    // Set up some constants
    struct ct_geom cg=mr->cg;
    struct recon_info ri=mr->ri;
    struct recon_params rp=mr->rp;

    const double da=0.0;
    const double dr=cg.src_to_det*rp.coll_slicewidth/(4.0*(cg.src_to_det-cg.r_f)*tan(cg.anode_angle));
    int n_proj=mr->ri.n_proj_pull/mr->ri.n_ffs;

    // We want to rebin the 1/4 or 1/8 detector offset away
    float kwic_central_channel=((float)cg.n_channels_oversampled-1.0f)/2.0f;

    // Allocate raw data arrays and final output array
    float * raw_1;
    raw_1=(float*)malloc(cg.n_channels*cg.n_rows*n_proj*sizeof(float));
    float * raw_2;
    raw_2=(float*)malloc(cg.n_channels*cg.n_rows*n_proj*sizeof(float));
    
    float * h_output;
    h_output=(float*)malloc(cg.n_channels_oversampled*cg.n_rows*n_proj*sizeof(float));

    // Split raw data by focal spot
    for (int i=0;i<n_proj;i++){
	for (int j=0;j<cg.n_rows_raw;j++){
	    for (int k=0;k<cg.n_channels;k++){
		int out_idx=cg.n_channels*cg.n_rows_raw*i+cg.n_channels*j+k;
		int in_idx_ffs1=cg.n_channels*cg.n_rows_raw*(2*i)+cg.n_channels*j+k;
		int in_idx_ffs2=cg.n_channels*cg.n_rows_raw*(2*i+1)+cg.n_channels*j+k;

		raw_1[out_idx]=mr->ctd.raw[in_idx_ffs1];
		raw_2[out_idx]=mr->ctd.raw[in_idx_ffs2];
	    }
	}
    }

    // Allocate and compute beta lookup tables
    float * beta_lookup_1;
    float * beta_lookup_2;
    beta_lookup_1=(float*)malloc(cg.n_channels*sizeof(float));
    beta_lookup_2=(float*)malloc(cg.n_channels*sizeof(float));
    for (int i=0;i<cg.n_channels;i++){
	beta_lookup_1[i]=beta_rk(da,-dr,i,0,cg);
	beta_lookup_2[i]=beta_rk(da, dr,i,0,cg);
    }    

    // Precompute betas and beta indices
    float * betas_1;
    float * betas_2;
    betas_1=(float*)malloc(cg.n_channels_oversampled*sizeof(float));
    betas_2=(float*)malloc(cg.n_channels_oversampled*sizeof(float));
    float * beta_indices_1;
    float * beta_indices_2;
    beta_indices_1=(float*)malloc(cg.n_channels_oversampled*sizeof(float));
    beta_indices_2=(float*)malloc(cg.n_channels_oversampled*sizeof(float));
    for (int i=0;i<cg.n_channels_oversampled;i++){
	betas_1[i]=asin(((float)i-kwic_central_channel)*(rp.recon_fov/(cg.n_channels_oversampled-1))/r_fr(0.0f,-dr,cg));
	betas_2[i]=asin(((float)i-kwic_central_channel)*(rp.recon_fov/(cg.n_channels_oversampled-1))/r_fr(0.0f, dr,cg));
	beta_indices_1[i]=get_beta_idx(betas_1[i],beta_lookup_1,cg.n_channels);
	beta_indices_2[i]=get_beta_idx(betas_2[i],beta_lookup_2,cg.n_channels);	
    }

    // Set up interpolation array dims
    struct array_dims dim;
    dim.idx1=cg.n_channels;
    dim.idx2=cg.n_rows_raw;
    dim.idx3=n_proj;

    for (int proj=0;proj<n_proj;proj++){
	printf("Projection %d/%d\n",proj,n_proj);
	for (int row=0;row<cg.n_rows_raw;row++){
	    for (int channel=0;channel<cg.n_channels_oversampled;channel++){

		// da=0, dr= -dr
 		//float beta_1=asin((channel-2.0f*cg.central_channel)*(cg.fan_angle_increment/2.0f)*cg.r_f/r_fr(0.0f,-dr,cg));
		float beta_1=betas_1[channel];
		float alpha_idx_1=ri.n_ffs*(proj)-beta_1*cg.n_proj_ffs/(2.0f*pi)-d_alpha_r(da,-dr,cg)*cg.n_proj_ffs/(2.0f*pi);
		float beta_idx_1=beta_indices_1[channel];

		// da=0, dr= +dr
		//float beta_2=asin((channel-2.0f*cg.central_channel)*(cg.fan_angle_increment/2.0f)*cg.r_f/r_fr(0.0f,dr,cg));
		float beta_2=betas_2[channel];
		float alpha_idx_2=ri.n_ffs*(proj)-beta_2*cg.n_proj_ffs/(2.0f*pi)-d_alpha_r(da,dr,cg)*cg.n_proj_ffs/(2.0f*pi);
		float beta_idx_2=beta_indices_2[channel];

		// Rescale alpha indices to properly index the raw arrays as 0, 1, 2, 3, ...
		alpha_idx_1=alpha_idx_1/2.0f; // raw_1 contains alpha projections 0, 2, 4, 6, ...
		alpha_idx_2=(alpha_idx_2-1.0f)/2.0f; // raw_2 contains projections 1, 3, 5, 7, ...

		int out_idx_1=cg.n_channels_oversampled*cg.n_rows*proj + cg.n_channels_oversampled*  2*row   + channel;
		int out_idx_2=cg.n_channels_oversampled*cg.n_rows*proj + cg.n_channels_oversampled*(2*row+1) + channel;

		h_output[out_idx_1]=interp3(raw_1,dim,beta_idx_1,row,alpha_idx_1);
		h_output[out_idx_2]=interp3(raw_2,dim,beta_idx_2,row,alpha_idx_2);
	    }
	}
    }

    //Copy data into our mr structure, skipping initial truncated projections
    size_t offset=cg.add_projections;
    for (int i=0;i<cg.n_channels_oversampled;i++){
	for (int j=0;j<cg.n_rows;j++){
	    for (int k=0;k<(mr->ri.n_proj_pull/mr->ri.n_ffs-2*cg.add_projections);k++){
		mr->ctd.rebin[k*cg.n_channels_oversampled*cg.n_rows+j*cg.n_channels_oversampled+i]=h_output[(k+offset)*cg.n_channels_oversampled*cg.n_rows+j*cg.n_channels_oversampled+i];
	    }
	}
    }

//    printf("Filtering...\n");
//    
//    // Load and run filter
//    float * h_filter=(float*)calloc(2*cg.n_channels_oversampled,sizeof(float));
//    load_filter(h_filter,mr);
//
//    for (int i=0;i<(n_proj-2*cg.add_projections);i++){
//	for (int j=0;j<cg.n_rows;j++){
//	    int row_start_idx=i*cg.n_channels_oversampled*cg.n_rows+cg.n_channels_oversampled*j;
//	    filter_cpu(&mr->ctd.rebin[row_start_idx],h_filter,cg.n_channels_oversampled);
//	}
//    }
    
    // Check "testing" flag, write rebin to disk if set
    if (mr->flags.testing){
	char fullpath[4096+255];
	strcpy(fullpath,mr->homedir);
	strcat(fullpath,"/Desktop/rebin.ct_test");
	FILE * outfile=fopen(fullpath,"w");
	fwrite(mr->ctd.rebin,sizeof(float),cg.n_channels_oversampled*cg.n_rows*(mr->ri.n_proj_pull-2*cg.add_projections_ffs)/mr->ri.n_ffs,outfile);
	fclose(outfile);
    }

    free(raw_1);
    free(raw_2);
    free(beta_lookup_1);
    free(beta_lookup_2);
    free(h_output);
//    free(h_filter);

}

void rebin_affs_cpu(struct recon_metadata *mr){
    // Set up some constants
    struct ct_geom cg=mr->cg;
    struct recon_info ri=mr->ri;
    struct recon_params rp=mr->rp;

    const double da=cg.src_to_det*cg.r_f*cg.fan_angle_increment/(4.0f*(cg.src_to_det-cg.r_f));
    const double dr=cg.src_to_det*rp.coll_slicewidth/(4.0*(cg.src_to_det-cg.r_f)*tan(cg.anode_angle));
    int n_proj=mr->ri.n_proj_pull/mr->ri.n_ffs;

    // Allocate raw data arrays and final output array
    float * raw_1;
    raw_1=(float*)malloc(cg.n_channels*cg.n_rows_raw*n_proj*sizeof(float));
    float * raw_2;
    raw_2=(float*)malloc(cg.n_channels*cg.n_rows_raw*n_proj*sizeof(float));
    float * raw_3;
    raw_3=(float*)malloc(cg.n_channels*cg.n_rows_raw*n_proj*sizeof(float));
    float * raw_4;
    raw_4=(float*)malloc(cg.n_channels*cg.n_rows_raw*n_proj*sizeof(float));

    float * rebin_t_1;
    rebin_t_1=(float*)malloc(cg.n_channels_oversampled*cg.n_rows_raw*n_proj*sizeof(float));
    float * rebin_t_2;
    rebin_t_2=(float*)malloc(cg.n_channels_oversampled*cg.n_rows_raw*n_proj*sizeof(float));
    
    float * h_output;
    h_output=(float*)malloc(cg.n_channels_oversampled*cg.n_rows*n_proj*sizeof(float));

    // Split raw data by focal spot
    for (int i=0;i<n_proj;i++){
	for (int j=0;j<cg.n_rows_raw;j++){
	    for (int k=0;k<cg.n_channels;k++){
		int out_idx=cg.n_channels*cg.n_rows_raw*i+cg.n_channels*j+k;
		int in_idx_ffs1=cg.n_channels*cg.n_rows_raw*(4*i)+cg.n_channels*j+k;
		int in_idx_ffs2=cg.n_channels*cg.n_rows_raw*(4*i+1)+cg.n_channels*j+k;
		int in_idx_ffs3=cg.n_channels*cg.n_rows_raw*(4*i+2)+cg.n_channels*j+k;
		int in_idx_ffs4=cg.n_channels*cg.n_rows_raw*(4*i+3)+cg.n_channels*j+k;
		raw_1[out_idx]=mr->ctd.raw[in_idx_ffs1];
		raw_2[out_idx]=mr->ctd.raw[in_idx_ffs2];
		raw_3[out_idx]=mr->ctd.raw[in_idx_ffs3];
		raw_4[out_idx]=mr->ctd.raw[in_idx_ffs4];
	    }
	}
    }

    FILE * out;
    out=fopen("/home/john/Desktop/raw_1.txt","w");
    fwrite(raw_1,sizeof(float),cg.n_channels*cg.n_rows_raw*n_proj,out);
    fclose(out);
    out=fopen("/home/john/Desktop/raw_2.txt","w");
    fwrite(raw_2,sizeof(float),cg.n_channels*cg.n_rows_raw*n_proj,out);
    fclose(out);
    out=fopen("/home/john/Desktop/raw_3.txt","w");
    fwrite(raw_3,sizeof(float),cg.n_channels*cg.n_rows_raw*n_proj,out);
    fclose(out);
    out=fopen("/home/john/Desktop/raw_4.txt","w");
    fwrite(raw_4,sizeof(float),cg.n_channels*cg.n_rows_raw*n_proj,out);
    fclose(out);
    
    struct array_dims dim;
    dim.idx1=cg.n_channels;
    dim.idx2=cg.n_rows_raw;
    dim.idx3=n_proj;

    // Allocate beta lookup tables
    // Allocate and compute beta lookup tables
    float * beta_lookup_1;
    float * beta_lookup_2;
    beta_lookup_1=(float*)malloc(sizeof(float)*cg.n_channels_oversampled);
    beta_lookup_2=(float*)malloc(sizeof(float)*cg.n_channels_oversampled);

    // Rebin projections
    for (int proj=0;proj<n_proj;proj++){
	for (int row=0;row<cg.n_rows_raw;row++){
	    for (int channel=0;channel<cg.n_channels;channel++){
		
		int out_idx_1=proj*cg.n_channels_oversampled*cg.n_rows_raw+row*cg.n_channels_oversampled+2*channel;
		int out_idx_2=proj*cg.n_channels_oversampled*cg.n_rows_raw+row*cg.n_channels_oversampled+2*channel+1;
		int out_idx_3=proj*cg.n_channels_oversampled*cg.n_rows_raw+row*cg.n_channels_oversampled+2*channel;
		int out_idx_4=proj*cg.n_channels_oversampled*cg.n_rows_raw+row*cg.n_channels_oversampled+2*channel+1;

		// -dr >>>>>
		// +da
		float beta_1 = beta_rk(da,-dr,channel,0,cg);
		float alpha_idx_1=ri.n_ffs*(proj)-beta_1*cg.n_proj_ffs/(2.0f*pi)-d_alpha_r(da,-dr,cg)*cg.n_proj_ffs/(2.0f*pi);
		// -da
		float beta_2 = beta_rk(-da,-dr,channel,0,cg);
		float alpha_idx_2=ri.n_ffs*(proj)-beta_2*cg.n_proj_ffs/(2.0f*pi)-d_alpha_r(-da,-dr,cg)*cg.n_proj_ffs/(2.0f*pi);
		beta_lookup_1[2*channel]=beta_1;
		beta_lookup_1[2*channel+1]=beta_2;		
		// <<<<< -dr

		// +dr >>>>>
		// +da
		float beta_3 = beta_rk(da,dr,channel,0,cg);
		float alpha_idx_3=ri.n_ffs*(proj)-beta_1*cg.n_proj_ffs/(2.0f*pi)-d_alpha_r(da,dr,cg)*cg.n_proj_ffs/(2.0f*pi);
		// -da
		float beta_4 = beta_rk(-da,dr,channel,0,cg);
		float alpha_idx_4=ri.n_ffs*(proj)-beta_2*cg.n_proj_ffs/(2.0f*pi)-d_alpha_r(-da,dr,cg)*cg.n_proj_ffs/(2.0f*pi);
		beta_lookup_2[2*channel]=beta_3;
		beta_lookup_2[2*channel+1]=beta_4;
		// <<<<< +dr

		// Rescale alpha indices to properly index the raw arrays as 0, 1, 2, 3, ...
		alpha_idx_1=    alpha_idx_1    /4.0f; // raw_1 contains projections 0, 4, 8, 12, ...
		alpha_idx_2=(alpha_idx_2-1.0f) /4.0f; // raw_2 contains projections 1, 5, 9, 13, ...
		alpha_idx_3=(alpha_idx_3-2.0f) /4.0f; // raw_3 contains projections 2, 6, 10, 14, ...
		alpha_idx_4=(alpha_idx_4-3.0f) /4.0f; // raw_4 contains projections 3, 7, 11, 15, ...
		
		rebin_t_1[out_idx_1]=interp3(raw_1,dim,channel,row,alpha_idx_1);
		rebin_t_1[out_idx_2]=interp3(raw_2,dim,channel,row,alpha_idx_2);
		rebin_t_2[out_idx_3]=interp3(raw_3,dim,channel,row,alpha_idx_3);
		rebin_t_2[out_idx_4]=interp3(raw_4,dim,channel,row,alpha_idx_4);

	    }
	}
    }

    out=fopen("/home/john/Desktop/rebin_t_1.txt","w");
    fwrite(rebin_t_1,sizeof(float),cg.n_channels_oversampled*cg.n_rows_raw*n_proj,out);
    fclose(out);
    out=fopen("/home/john/Desktop/rebin_t_2.txt","w");
    fwrite(rebin_t_2,sizeof(float),cg.n_channels_oversampled*cg.n_rows_raw*n_proj,out);
    fclose(out);

    free(raw_1);
    free(raw_2);
    free(raw_3);
    free(raw_4);

    // Update the interpolation array dimensions since new array has twice as many channels
    dim.idx1*=2;

    // Rebin channels
    for (int proj=0;proj<n_proj;proj++){
	for (int row=0;row<cg.n_rows_raw;row++){
	    for (int channel=0;channel<cg.n_channels_oversampled;channel++){
		
		int out_idx_1=cg.n_channels_oversampled*cg.n_rows*proj+cg.n_channels_oversampled*  2*row  +channel;
		int out_idx_2=cg.n_channels_oversampled*cg.n_rows*proj+cg.n_channels_oversampled*(2*row+1)+channel;
		float beta_1 = asin((channel-2*cg.central_channel)*(cg.fan_angle_increment/2)*cg.r_f/r_fr(0.0f,-dr,cg));
		float beta_2 = asin((channel-2*cg.central_channel)*(cg.fan_angle_increment/2)*cg.r_f/r_fr(0.0f,dr,cg));
		float beta_idx_1=get_beta_idx(beta_1,beta_lookup_1,cg.n_channels_oversampled);
		float beta_idx_2=get_beta_idx(beta_2,beta_lookup_2,cg.n_channels_oversampled);
		
		h_output[out_idx_1]=interp3(rebin_t_1,dim,beta_idx_1,row,proj);
		h_output[out_idx_2]=interp3(rebin_t_2,dim,beta_idx_2,row,proj);
		
	    }
	}
    }

    //Copy data into our mr structure, skipping initial truncated projections
    size_t offset=cg.add_projections;
    for (int i=0;i<cg.n_channels_oversampled;i++){
	for (int j=0;j<cg.n_rows;j++){
	    for (int k=0;k<(mr->ri.n_proj_pull/mr->ri.n_ffs-2*cg.add_projections);k++){
		mr->ctd.rebin[k*cg.n_channels_oversampled*cg.n_rows+j*cg.n_channels_oversampled+i]=h_output[(k+offset)*cg.n_channels_oversampled*cg.n_rows+j*cg.n_channels_oversampled+i];
	    }
	}
    }

    printf("Filtering...\n");
    
    // Load and run filter
    float * h_filter=(float*)calloc(2*cg.n_channels_oversampled,sizeof(float));
    load_filter(h_filter,mr);

    for (int i=0;i<(n_proj-2*cg.add_projections);i++){
	for (int j=0;j<cg.n_rows;j++){
	    int row_start_idx=i*cg.n_channels_oversampled*cg.n_rows+cg.n_channels_oversampled*j;
	    filter_cpu(&mr->ctd.rebin[row_start_idx],h_filter,cg.n_channels_oversampled);
	}
    }
    
    // Check "testing" flag, write rebin to disk if set
    if (mr->flags.testing){
	char fullpath[4096+255];
	strcpy(fullpath,mr->homedir);
	strcat(fullpath,"/Desktop/rebin.ct_test");
	FILE * outfile=fopen(fullpath,"w");
	fwrite(mr->ctd.rebin,sizeof(float),cg.n_channels_oversampled*cg.n_rows*(mr->ri.n_proj_pull-2*cg.add_projections_ffs)/mr->ri.n_ffs,outfile);
	fclose(outfile);
    }
    
    free(beta_lookup_1);
    free(beta_lookup_2);
    free(rebin_t_1);
    free(rebin_t_2);    
    free(h_output);
}

void filter_cpu(float * row, float * filter, int N){
    // N is the number of elements in a row

    // Calculate padding
    int M=2*pow(2.0f,ceil(log2((float)N)));

    // Create two new padded/manipulated vectors from our inputs into fftw complex arrays
    fftw_complex * R = (fftw_complex*)fftw_malloc(sizeof(fftw_complex)*M);
    memset(R,0,M*sizeof(fftw_complex));
    for (int i=0;i<N;i++){
	R[i]=row[i];
    }

    fftw_complex * F=(fftw_complex*)fftw_malloc(sizeof(fftw_complex)*M);
    memset(F,0,M*sizeof(fftw_complex));
    
    for (int i=0;i<N;i++){
	F[i]=filter[(int)floor((2.0f*N-1.0f)/2.0f)+1+i];
    }
    for (int i=(M-N+1);i<M;i++){
	F[i]=filter[i-(M-N+1)+1];
    }

    // Allocate complex output vectors for row and filter FFTs
    fftw_complex * R_fourier=(fftw_complex*)fftw_malloc(sizeof(fftw_complex)*M);
    fftw_complex * F_fourier=(fftw_complex*)fftw_malloc(sizeof(fftw_complex)*M);

    // Create plans and execute FFTs
    fftw_plan p_R,p_F;
    p_R=fftw_plan_dft_1d(M,R,R_fourier,FFTW_FORWARD,FFTW_ESTIMATE);
    p_F=fftw_plan_dft_1d(M,F,F_fourier,FFTW_FORWARD,FFTW_ESTIMATE);
    fftw_execute(p_R);
    fftw_execute(p_F);

    //Multiply row and filter into output array
    fftw_complex * O_fourier=(fftw_complex*)fftw_malloc(sizeof(fftw_complex)*M);
    for (int i=0;i<M;i++){
	O_fourier[i]=R_fourier[i]*F_fourier[i];
    }

    //Prep final output array and plan, then execute
    fftw_complex * O=(fftw_complex*)fftw_malloc(sizeof(fftw_complex)*M);
    fftw_plan p_O;
    p_O=fftw_plan_dft_1d(M,O_fourier,O,FFTW_BACKWARD,FFTW_ESTIMATE);
    fftw_execute(p_O);

    //Copy real portion of final result into source row
    for (int i=0;i<N;i++){
	row[i]=(1.0f/(float)M)*(float)creal(O[i]);
    }

    // Clean up
    fftw_destroy_plan(p_R);
    fftw_destroy_plan(p_F);
    fftw_destroy_plan(p_O);    

    fftw_free(F);
    fftw_free(F_fourier);
    fftw_free(R);
    fftw_free(R_fourier);
    fftw_free(O);
    fftw_free(O_fourier);    
}

void load_filter(float * f_array,struct recon_metadata * mr){
    struct ct_geom cg=mr->cg;
    struct recon_params rp=mr->rp;

    char fullpath[4096+255]={0};

    FILE * filter_file;
    switch (rp.recon_kernel){
    case -100:{
	sprintf(fullpath,"%s/resources/filters/f_%i_ramp.txt",mr->install_dir,cg.n_channels);
	break;}
    case -1:{
	sprintf(fullpath,"%s/resources/filters/f_%i_exp.txt",mr->install_dir,cg.n_channels);
	break;}
    case 1:{
	sprintf(fullpath,"%s/resources/filters/f_%i_smooth.txt",mr->install_dir,cg.n_channels);
	break;}
    case 2:{
	sprintf(fullpath,"%s/resources/filters/f_%i_medium.txt",mr->install_dir,cg.n_channels);
	break;}
    case 3:{
	sprintf(fullpath,"%s/resources/filters/f_%i_sharp.txt",mr->install_dir,cg.n_channels);
	break;}
    default:{
	sprintf(fullpath,"%s/resources/filters/f_%i_b%i.txt",mr->install_dir,cg.n_channels,rp.recon_kernel);
	break;}
    }

    filter_file=fopen(fullpath,"r");

    fread(f_array,sizeof(float),2*cg.n_channels_oversampled,filter_file);
    fclose(filter_file);
}
