/* Copyright (c) 2007 Joint Institute for VLBI in Europe (Netherlands)
 * All rights reserved.
 * 
 * Author(s): Ruud Oerlemans <Oerlemans@JIVE.nl>, 2007
 * 
 * $Id$
 *
 */

#include "DelayCorrection.h"


//Allocate arrays, initialise parameters
DelayCorrection::DelayCorrection(Log_writer &lg_wrtr)
  : log_writer(lg_wrtr)
{
}
//Allocate arrays, initialise parameters
DelayCorrection::DelayCorrection(GenP &GenPrms_, 
                                 StaP *StaPrms_, 
                                 Log_writer &lg_wrtr)
  : log_writer(lg_wrtr)
{
  set_parameters(GenPrms_, StaPrms_);
}

//Allocate arrays, initialise parameters
void DelayCorrection::set_parameters(GenP &GenPrms, StaP *StaPrms_)
{
  StaPrms     = StaPrms_;

  nstations   = GenPrms.get_nstations();
  n2fftDC     = GenPrms.get_lsegm();
  SR          = 2.0*GenPrms.get_bwfl()*GenPrms.get_ovrfl();
  BufSize     = BufTime * (int)(SR/1000000.0);
  tbs         = 1.0/SR;
  Nsegm2DC    = BufSize/n2fftDC;

  sideband    = GenPrms.get_sideband();

  Nf          = n2fftDC/2+1; //number of frequencies
  double dfr  = 1.0/(n2fftDC*tbs); // delta frequency
  fs          = new double[Nf]; // frequency array

  for (int jf=0; jf<Nf; jf++) {
    //frequency scale in the segment
    fs[jf]=sideband*(jf*dfr-0.5*GenPrms.get_bwfl()-GenPrms.get_foffset());
  }

  foffset     = GenPrms.get_foffset();
  bwfl        = GenPrms.get_bwfl();
  ovrfl       = GenPrms.get_ovrfl();
  startf      = GenPrms.get_startf();
  skyfreq     = GenPrms.get_skyfreq();

  n2fftcorr   = GenPrms.get_n2fft();
  
  segm = new double*[nstations];
  Bufs = new double*[nstations];
  dcBufs = new double*[nstations];
  dcBufPrev =new double*[nstations];
  for (int sn=0; sn<nstations; sn++){
    segm[sn] = new double[n2fftcorr];
    Bufs[sn] = new double[BufSize];
    dcBufs[sn] = new double[3*BufSize];
    dcBufPrev[sn] = new double[2*BufSize];
  }
  
  timePtr = GenPrms.get_usStart();//set timePtr to start for delay
  BufPtr = BufSize;//set read pointer to end of Bufs, because Bufs not filled
  
  //arrays and plans for delay correction
  sls   = new fftw_complex[n2fftDC];
  spls  = new fftw_complex[n2fftDC];
  planT2F = fftw_plan_dft_1d(n2fftDC, sls, spls, FFTW_BACKWARD, FFTW_ESTIMATE);
  planF2T = fftw_plan_dft_1d(n2fftDC, spls, sls, FFTW_FORWARD,  FFTW_ESTIMATE);
  //TODO RHJO: ask SP why not use fftw_plan_r2c_1d and fftw_plan_c2r_1d. Try!
  //4b) and 4c) probably not necessary anymore


  //set vector sizes
  delTbl.resize(nstations);//delay tables 
  sample_reader.resize(nstations,NULL);//sample_readers


  data_frame = new double *[nstations];
  df_length = new INT32 [nstations];//frame lengths
  df_counter = new INT32 [nstations];//frame counters

  for (int sn=0; sn<nstations; sn++){
    //TODO RHJO add datatype check for other data types
    df_length[sn] = frameMk4*StaPrms[sn].get_fo();//initialise
    df_counter[sn] = df_length[sn];//set counter to end of frame
    data_frame[sn] = new double[df_length[sn]];
  }
  

}


//De-allocate arrays and destroy plans
DelayCorrection::~DelayCorrection()
{

  delete [] fs;

  for (int sn=0; sn<nstations; sn++){
    delete [] segm[sn];
    delete [] Bufs[sn];
    delete [] dcBufs[sn];
    delete [] data_frame[sn];
  }
  delete [] segm;  
  delete [] Bufs;
  delete [] dcBufs;
  delete [] df_length;
  delete [] df_counter;

  fftw_destroy_plan(planF2T);
  fftw_destroy_plan(planT2F);
  
  delete [] spls;
  delete [] sls;

}



//set local data reader parameter
void DelayCorrection::set_sample_reader(int sn, Bits_to_float_converter *sample_reader_)
{
  sample_reader[sn]=sample_reader_;
}

void DelayCorrection::set_start_time(INT64 us_start) {
  timePtr = us_start;//set timePtr to start for delay
}


//go to desired position in input reader for station sn
bool DelayCorrection::init_reader(int sn, INT64 startIS)
{
  df_counter[sn] = df_length[sn]; //set counter to end of frame
  BufPtr = BufSize;//set read pointer to end of Bufs, because Bufs not filled

  //initialise dcBufPrev with data from input channel (can be Mk4 file)
  for (int i=0; i<2*BufSize; i++) {
    if (df_counter[sn] == df_length[sn]) {
      //fill data_frame if data frame counter at end of frame
      //TODO RHJO implement data type check for other data type

      int status = fill_Mk4frame(sn,*sample_reader[sn],data_frame, StaPrms[sn]);
      if (status < 0) return false;
      df_counter[sn] = 0;
    }
    dcBufPrev[sn][i]=data_frame[sn][df_counter[sn]];
    df_counter[sn]++;
  }
  
  return true;
}


// fills the next segment to be processed by correlator core.
bool DelayCorrection::fill_segment()
{
  //(re)fill Bufs when all data in Bufs is processed
  if ( (BufPtr + n2fftcorr) > BufSize ) {
    if (!fill_Bufs()) return false;
    timePtr=timePtr+BufTime;
    BufPtr=0;
  }
  //fill segm using delay corrected data in Bufs
  for (int i = 0; i < nstations; i++){
    for (int j = 0; j < n2fftcorr; j++){
      segm[i][j] = Bufs[i][j+ BufPtr];
    }
  }
  BufPtr=BufPtr+n2fftcorr;

  return true;
}


// returns pointer to segment with delay corrected data.
double **DelayCorrection::get_segment()
{
  return segm;
}


//Fills Bufs with delay corrected data. This function has to be called
//every time all data in Bufs are processed.
bool DelayCorrection::fill_Bufs()
{
//  double Time; //time in micro seconds
  INT64  Time; //time in micro seconds
  double Cdel;
  double phi;
  double tmpR;
  double tmpI;
  double sqrtN2fft = sqrt((double) n2fftDC);
  double dfs;

  //temporary variables used to avoid recalculation of parameters in loops 
  double tmpA, tmpB, tmpC, tmp1, tmp2, tmp3, tmp4;
  INT32  tmpINT32;

  int jshift; //address shift due to signal delay wrt Earth center

  tmpA = -2.0*M_PI*(skyfreq + startf + bwfl*0.5);
  tmpB = tbs*1000000;
  tmpC = n2fftDC*tmpB;

  INT64 prev_time_for_fdel=-1;             // Time is always positive
  double prev_cos_fdel=0, prev_sin_fdel=1; // Initialise to remove warning
  
  for (int sn=0; sn<nstations; sn++){
  
    //fill part 1 and 2 of dcBufs with data from dcBufPrev
    for (int i=0; i<2*BufSize; i++) dcBufs[sn][i]=dcBufPrev[sn][i];
    
    //fill part 3 of dcBufs with data from input channel (can be Mk4File)
    for (int i=2*BufSize; i<3*BufSize; i++) {
      if (df_counter[sn] == df_length[sn]) {
        //fill data frame if data frame counter is at end of frame
        int nBytes = fill_Mk4frame(sn,*sample_reader[sn],data_frame,StaPrms[sn]);
        if (nBytes <= 0) {
          return false;
        }
        df_counter[sn] = 0;//reset FrameCounter for station sn
      }
      //fill remaining of dcBufs with data from Mk4file
      dcBufs[sn][i]=data_frame[sn][df_counter[sn]];
      df_counter[sn]++;
    }
    
    //apply delay and phase corrections for all segments (n2fftDC long)
    //in other words process data in dcBufs, output in Bufs
    for (int jsegm=0; jsegm<Nsegm2DC; jsegm++) {


      //Time = timePtr + (INT64)(jsegm*n2fftDC*tbs*1000000); //micro sec 
      Time = timePtr + (INT64)(jsegm*tmpC); //micro sec 
      Cdel = delTbl[sn].calcDelay(Time, DelayTable::Cdel);
      
      // 1)calculate the address shift due to time delay for the current segment
      jshift = (INT64)(Cdel/tbs+0.5);
      
      tmpINT32 = 2*BufSize + jshift + jsegm*n2fftDC;
      // 2)apply the address shift when filling the complex sls array
      for (int jl=0; jl<n2fftDC; jl++){
        //sls[jl][0] = dcBufs[sn][2*BufSize + jshift + jsegm*n2fftDC + jl];
        sls[jl][0] = dcBufs[sn][tmpINT32 + jl];
        sls[jl][1] = 0.0;
      }

      // 3) execute the complex to complex FFT, from Time to Frequency domain
      //    input: sls. output spls
      fftw_execute(planT2F);
      
      // 4a)apply normalization
      for (int jl=0; jl<n2fftDC/2+1; jl++){
        spls[jl][0] = spls[jl][0] / sqrtN2fft;
        spls[jl][1] = spls[jl][1] / sqrtN2fft;
      }
      
      // 4b)multiply element 0 and n2fftDC/2 by 0.5
      //    to avoid jumps at segment borders
      spls[0][0]=0.5*spls[0][0];
      spls[0][1]=0.5*spls[0][1];
      spls[n2fftDC/2][0]=0.5*spls[n2fftDC/2][0];//Nyquist
      spls[n2fftDC/2][1]=0.5*spls[n2fftDC/2][1];
      
      // 4c) zero the unused subband
      for (int jl=n2fftDC/2+1;jl<n2fftDC;jl++){
        spls[jl][0] = 0.0;
        spls[jl][1] = 0.0;
      }

      
      // 5a)calculate the fract bit shift (=phase corrections in freq domain)
      //Time = timePtr + (INT64)(jsegm*n2fftDC*tbs*1000000 + n2fftDC/2*tbs*1000000);
      Time = timePtr + (INT64)(tmpC*(jsegm+0.5));
      Cdel = delTbl[sn].calcDelay(Time, DelayTable::Cdel);
      dfs  = Cdel/tbs - floor(Cdel/tbs + 0.5);

      tmp1 = -2.0*M_PI*dfs*tbs;
      tmp2 = 0.5*M_PI*jshift/ovrfl;
      // 5b)apply phase correction in frequency range
      for (int jf = 0; jf < Nf; jf++){
        //phi  = -2.0*M_PI*dfs*tbs*fs[jf] + 0.5*M_PI*jshift/ovrfl;
        phi  = tmp1*fs[jf] + tmp2;
        tmp3=cos(phi);
        tmp4=sin(phi);
        tmpR = spls[jf][0];
        tmpI = spls[jf][1];
        spls[jf][0] = tmpR*tmp3-tmpI*tmp4;
        spls[jf][1] = tmpR*tmp4+tmpI*tmp3;
      }
      
      // 6a)execute the complex to complex FFT, from Frequency to Time domain
      //    input: spls. output sls
      fftw_execute(planF2T);
      
      double Fdel; 
      for (int jl=0;jl<n2fftDC;jl++) {
        // 6b)apply normalization and multiply by 2.0
        sls[jl][0] = 2.0*sls[jl][0] / sqrtN2fft;
        
        // 7)subtract dopplers and put real part in Bufs for the current segment
        //Time = timePtr + (INT64)(jsegm*n2fftDC*tbs*1000000 + jl*tbs*1000000);
        Time = timePtr + (INT64)(jsegm*tmpC + jl*tmpB);
        if (Time != prev_time_for_fdel) {
          // NGHK: CHECK: The precision in Time is not sufficient to compute  
          // the exact time delay for each sample. The precision is in us 
          // (1e-6), whereas the sample rate can be 16MHz (16e6). 
          Fdel = delTbl[sn].calcDelay(Time, DelayTable::Fdel);
          phi  = -2.0*M_PI*(skyfreq + startf + sideband*bwfl*0.5)*Fdel;
          prev_time_for_fdel = Time;
          prev_cos_fdel = cos(phi);
          prev_sin_fdel = sin(phi);
        }

        Bufs[sn][n2fftDC*jsegm+jl]=sls[jl][0]*prev_cos_fdel-sls[jl][1]*prev_sin_fdel;
      }


/*
//TODO RHJO: optimized code, calculation of cos(phi) and sin(phi) per sample
//           very time comsuming. cos(phi) and sin(phi) now calculated once
//           per segment. Fringes a little lower but calculation now 24 sec
//           in stead of 40 sec. 
//           Discuss this change with Mark and Sergei.
      Time = timePtr + (INT64)(jsegm*tmpC + n2fftDC/2*tmpB);
      phi  = tmpA*delTbl[sn].calcDelay(Time, DelayTable::Fdel);
      tmp1 = sin(phi);
      tmp2 = cos(phi);

      for (int jl=0;jl<n2fftDC;jl++) {

        // 6b)apply normalization and multiply by 2.0
        sls[jl][0] = 2.0*sls[jl][0] / sqrtN2fft;

        // 7) fringe stopping
        Bufs[sn][n2fftDC*jsegm+jl]=sls[jl][0]*tmp2-sls[jl][1]*tmp1;
      }
*/      
    
    }
    
    //fill dcBufsPrev with part 2 and 3 from dcBufs.
    //in other words: remember for filling the next Bufs
    for (int i=0; i<2*BufSize; i++) 
      dcBufPrev[sn][i] = dcBufs[sn][BufSize+i];

  }

  return true;
}



Log_writer& DelayCorrection::get_log_writer()
{
  return log_writer;
}

//set local delay table parameter
bool DelayCorrection::set_delay_table(int sn, DelayTable &delay_table)
{
  assert(sn >= 0);
  assert((size_t)sn < delTbl.size());
  delTbl[sn]=delay_table;
  
  return true;
}

