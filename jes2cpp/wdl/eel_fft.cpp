/*
  WDL - eel_fft.cpp
  Copyright (C) 2006 and later Cockos Incorporated

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

*/

#include "eel_fft.h"

//#define EEL_SUPER_FAST_FFT_REORDERING // quite a bit faster (50-100%) than "normal", but uses a 256kb lookup
//#define EEL_SLOW_FFT_REORDERING // 20%-80% slower than normal, alloca() use, no reason to ever use this

#ifdef EEL_SUPER_FAST_FFT_REORDERING
static int *fft_reorder_table_for_bitsize(int bitsz)
{
  static int s_tab[ (2 << EEL_FFT_MAXBITLEN) + 24*(EEL_FFT_MAXBITLEN-EEL_FFT_MINBITLEN+1) ]; // big 256kb table, ugh
  if (bitsz<=EEL_FFT_MINBITLEN) return s_tab;
  return s_tab + (1<<bitsz) + (bitsz-EEL_FFT_MINBITLEN) * 24;
}
static void fft_make_reorder_table(int bitsz, int *tab)
{
  const int fft_sz=1<<bitsz;
  char flag[1<<EEL_FFT_MAXBITLEN];
  int x;
  int *tabstart = tab;
  memset(flag,0,fft_sz);

  for (x=0;x<fft_sz;x++)
  {
    int fx;
    if (!flag[x] && (fx=WDL_fft_permute(fft_sz,x))!=x)
    {
      flag[x]=1;
      *tab++ = x;
      do
      {
        flag[fx]=1;
        *tab++ = fx;
        fx = WDL_fft_permute(fft_sz, fx);
      }
      while (fx != x);
      *tab++ = 0; // delimit a run
    }
    else flag[x]=1;
  }
  *tab++ = 0; // doublenull terminated
}

static void fft_reorder_buffer(int bitsz, WDL_FFT_COMPLEX *data, int fwd)
{
  const int *tab=fft_reorder_table_for_bitsize(bitsz);
  if (!fwd)
  {
    while (*tab)
    {
      const int sidx=*tab++;
      WDL_FFT_COMPLEX a=data[sidx];
      for (;;)
      {
        WDL_FFT_COMPLEX ta;
        const int idx=*tab++;
        if (!idx) break;
        ta=data[idx];
        data[idx]=a;
        a=ta;
      }
      data[sidx] = a;
    }
  }
  else
  {
    while (*tab)
    {
      const int sidx=*tab++;
      int lidx = sidx;
      const WDL_FFT_COMPLEX sta=data[lidx];
      for (;;)
      {
        const int idx=*tab++;
        if (!idx) break;

        data[lidx]=data[idx];
        lidx=idx;
      }
      data[lidx] = sta;
    }
  }
//  return 1;
}
#else
#ifndef EEL_SLOW_FFT_REORDERING
 // moderate speed mode, minus the big 256k table

static void fft_reorder_buffer(int bitsz, WDL_FFT_COMPLEX *data, int fwd)
{
  // this is a good compromise, quite a bit faster than out of place reordering, but no separate 256kb lookup required
  /*
  these generated via:
      static void fft_make_reorder_table(int bitsz)
      {
        int fft_sz=1<<bitsz,x;
        char flag[65536]={0,};
        printf("static const int tab%d[]={ ",fft_sz);
        for (x=0;x<fft_sz;x++)
        {
          int fx;
          if (!flag[x] && (fx=WDL_fft_permute(fft_sz,x))!=x)
          {
            printf("%d, ",x);
            do { flag[fx]=1; fx = WDL_fft_permute(fft_sz, fx); } while (fx != x);
          }
          flag[x]=1;
        }
        printf(" 0 };\n");
      }
      */

  static const int tab4_8_32[]={ 1,  0 };
  static const int tab16[]={ 1, 3,  0 };
  static const int tab64[]={ 1, 3, 9,  0 };
  static const int tab128[]={ 1, 3, 4, 9, 14,  0 };
  static const int tab256[]={ 1, 3, 6, 12, 13, 14, 19,  0 };
  static const int tab512[]={ 1, 4, 7, 9, 18, 50, 115,  0 };
  static const int tab1024[]={ 1, 3, 4, 25, 26, 77, 79,  0 };
  static const int tab2048[]={ 1, 58, 59, 106, 135, 206, 210, 212,  0 };
  static const int tab4096[]={ 1, 3, 12, 25, 54, 221, 313, 431, 453,  0 };
  static const int tab8192[]={ 1, 12, 18, 26, 30, 100, 101, 106, 113, 144, 150, 237, 244, 247, 386, 468, 513, 1210, 4839, 0 };
  static const int tab16384[]={ 1, 3, 6, 24, 1219,  0 };
  static const int tab32768[]={ 1, 3, 4, 7, 13, 18, 31, 64, 113, 145, 203, 246, 594, 956, 1871, 2439, 4959, 19175,  0 };
  const int *tab;

  switch (bitsz)
  {
    case 1: return; // no reorder necessary
    case 2:
    case 3:
    case 5: tab = tab4_8_32; break;
    case 4: tab=tab16; break;
    case 6: tab=tab64; break;
    case 7: tab=tab128; break;
    case 8: tab=tab256; break;
    case 9: tab=tab512; break;
    case 10: tab=tab1024; break;
    case 11: tab=tab2048; break;
    case 12: tab=tab4096; break;
    case 13: tab=tab8192; break;
    case 14: tab=tab16384; break;
    case 15: tab=tab32768; break;
    default: return; // no reorder possible
  }

  const int fft_sz=1<<bitsz;
  const int *tb2 = WDL_fft_permute_tab(fft_sz);
  if (!tb2) return; // ugh

  if (!fwd)
  {
    while (*tab)
    {
      const int sidx=*tab++;
      WDL_FFT_COMPLEX a=data[sidx];
      int idx=sidx;
      for (;;)
      {
        WDL_FFT_COMPLEX ta;
        idx=tb2[idx];
        if (idx==sidx) break;
        ta=data[idx];
        data[idx]=a;
        a=ta;
      }
      data[sidx] = a;
    }
  }
  else
  {
    while (*tab)
    {
      const int sidx=*tab++;
      int lidx = sidx;
      const WDL_FFT_COMPLEX sta=data[lidx];
      for (;;)
      {
        const int idx=tb2[lidx];
        if (idx==sidx) break;

        data[lidx]=data[idx];
        lidx=idx;
      }
      data[lidx] = sta;
    }
  }
}

#endif // not fast ,not slow, just right

#endif

//#define TIMING
//#include "../timing.h"

// 0=fw, 1=iv, 2=fwreal, 3=ireal, 4=permutec, 6=permuter
// low bit: is inverse
// second bit: was isreal, but no longer used
// third bit: is permute
void FFT(int sizebits, EEL_F *data, int dir)
{
  if (dir >= 4 && dir < 8)
  {
    if (dir == 4 || dir == 5)
    {
      //timingEnter(0);
#if defined(EEL_SUPER_FAST_FFT_REORDERING) || !defined(EEL_SLOW_FFT_REORDERING)
      fft_reorder_buffer(sizebits,(WDL_FFT_COMPLEX*)data,dir==4);
#else
      // old blech
      const int flen=1<<sizebits;
      int x;
      EEL_F *tmp=(EEL_F*)alloca(sizeof(EEL_F)*flen*2);
    	const int flen2=flen+flen;
	    // reorder entries, now
      memcpy(tmp,data,sizeof(EEL_F)*flen*2);

      if (dir == 4)
      {
 	      for (x = 0; x < flen2; x += 2)
   	    {
   	      int y=WDL_fft_permute(flen,x/2)*2;
          data[x]=tmp[y];
   	      data[x+1]=tmp[y+1];
        }

      }
      else
      {
 	      for (x = 0; x < flen2; x += 2)
   	    {
   	      int y=WDL_fft_permute(flen,x/2)*2;
          data[y]=tmp[x];
   	      data[y+1]=tmp[x+1];
        }
      }
#endif
      //timingLeave(0);
    }
  }
	else if (dir >= 0 && dir < 2)
	{
    WDL_fft((WDL_FFT_COMPLEX*)data,1<<sizebits,dir&1);
	}
}


void EEL_fft_register()
{
  WDL_fft_init();
#if defined(EEL_SUPER_FAST_FFT_REORDERING)
  if (!fft_reorder_table_for_bitsize(EEL_FFT_MINBITLEN)[0])
  {
    int x;
    for (x=EEL_FFT_MINBITLEN;x<=EEL_FFT_MAXBITLEN;x++) fft_make_reorder_table(x,fft_reorder_table_for_bitsize(x));
  }
#endif
}

