#include "3d.h"

//#define DEBUG
//#define DEBUGCALLS
//#define DEBUGCOLORS
//#define DEBUGOOW
//#define DEBUGFOG
//#define NOFOG

#ifndef __cplusplus
#include <stdlib.h>
#include <string.h>
#include <math.h>
#if defined(DEBUG) | defined(DEBUGCALLS) | defined(DEBUGCOLORS) | defined(DEBUGOOW) | defined(DEBUGFOG)
#include <stdio.h>
#endif
#else
#include <cstdlib>
#include <cstring>
#include <cmath>
#if defined(DEBUG) | defined(DEBUGCALLS) | defined(DEBUGCOLORS) | defined(DEBUGOOW) | defined(DEBUGFOG)
#include <cstdio>
#endif
#endif

//Was only added in C99
#ifndef expf
#define expf exp
#endif

//Was only added in C99
#ifndef logf
#define logf log
#endif

//Was only added in C99
#ifndef powf
#define powf pow
#endif

#ifndef log2f
float log2f(float n)
{
	return logf(n) / logf(2.0);
}
#endif

float pow2f(float n)
{
	return n*n;
}

#define SOFTG_QUARK_VERSION_NUMBER		32

#define RGBBITS				11
#define RGBMAX				(1<<RGBBITS)

// For mode [T] :
#define COLORSCHEMEBITS		(3)
#define COLORSCHEMES		(1<<COLORSCHEMEBITS)

// For mode [S] :
#define SOLIDCOLORSCHEMES	(1<<8)

#define GR_COLORCOMBINE_CCRGB		1
#define GR_COLORCOMBINE_TEXTURE		4


/*  This module can work in one of three pixel modes :
 *   [T] textured            (unifiedpalettemode & (colormode = GR_COLORCOMBINE_TEXTURE))
 *   [S] solid               (unifiedpalettemode & (colormode = GR_COLORCOMBINE_CCRGB))
 *   [X] paletteless         (!unifiedpalettemode)
 */


//grTexInfo_t *texsource;
GuTexPalette texturepalette;
FxU8 *texdata;             // 16-bit RGB443 (RGB565 is converted in-place)
                           // OR
                           // 8-bit palette indices

FxU32 schemecolor;
FxU16 scheme;             // [T] 00000000ccc00000  format
                          // [S] cccccccc00000000  format

//currentpalette gets constructed from texturepalette in function FillCurrentPalette
FxU16 currentpalette[256];    // [T] -
                              // [S] -
                              // [X] bbbggggrrrr00000  format
unsigned int currentpaletteok; //Does currentpalette need rebuilding?

GrFog_t fogtable[GR_FOG_TABLE_SIZE];
GrColor_t fogcolor;

//fullpalette gets constructed in function BuildFullPalette
FxU32 *fullpalette;		// array of true colors indexed by 16-bit framebuffer pixels, and if fog is enable, there's GR_FOG_TABLE_SIZE palettes in a row

int texwmask, texhmask, texh1;
float stowbase;
FxU32 framew, frameh, firstcol, firstrow;
unsigned int framecount;
FxU16 *framebuffer;		// a pixel is :   [T] ttttttttccc00000    (t)exture, (c)olor scheme, (f)og
						// 'solid' mode : [S] cccccccc00000000    (c)olor, (f)og
						// paletteless :  [X] bbbggggrrrr00000    (r)ed, (g)reen, (b)lue, (f)og
float *depthbuffer; //Depth buffer (oow)
GrColorCombineFunction_t colormode;
unsigned int flatdisplay; //zero or GR_STWHINT_W_DIFF_TMU0
unsigned int unifiedpalettemode; //zero or softgUnifiedPalette
unsigned int texture_mode; //zero or softgRGB443Texture
unsigned int oow_table_mode;
unsigned int enableFog;

// For mode [S] :
FxU32 SchemeBaseColor[SOLIDCOLORSCHEMES];

// For mode [T] :
FxU32 SchemesUsageTime[COLORSCHEMES];

#if defined(DEBUG) | defined(DEBUGCALLS) | defined(DEBUGCOLORS) | defined(DEBUGOOW) | defined(DEBUGFOG)
#define logfilename "qrksoftg.log"
FILE* logfile = 0;
#endif

//Extract R, G, B from RGB888
#define COLOR_R(c)  ((c >> 16) & 0xFF)
#define COLOR_G(c)  ((c >> 8) & 0xFF)
#define COLOR_B(c)  ((c) & 0xFF)

#define c_macro(b,c)  (((b)*(FxU8)(c))>>12)

//Convert to RGB443, with the lower 5 bits zero (for fogbits).
//#define PACKCOLOR(c)  ((((c) & 0x0000F0)<<17) | (((c) & 0x00F000)<<13) | (((c) & 0xE00000)<<8))
//#define PACKCOLOR(c)  ((((c) & 0xF00000)<<1) | (((c) & 0x00F000)<<13) | (((c) & 0x0000E0)<<24))
#define PACKCOLOR(c)  ((((c) & 0xF00000)>>15) | (((c) & 0x00F000)>>3) | (((c) & 0x0000E0)<<8))

void FillCurrentPalette(void)
{
	unsigned int i;
	FxU32 c;

	#ifdef DEBUGCALLS
	fprintf(logfile, "FillCurrentPalette()\n");
	fflush(logfile);
	#endif

	if (!unifiedpalettemode)
	{
		if (schemecolor == 0xFFFFFF)
		{
			#ifdef DEBUGCOLORS
			fprintf(logfile, "FillCurrentPalette: NOT schemecolor");
			fflush(logfile);
			#endif
			for (i=0; i<256; i++)
			{
				c = texturepalette.data[i];
				currentpalette[i] = PACKCOLOR(c);
				#ifdef DEBUGCOLORS
				fprintf(logfile, "  %08x", currentpalette[i]);
				fflush(logfile);
				#endif
			}
			#ifdef DEBUGCOLORS
			fprintf(logfile, "\n");
			fflush(logfile);
			#endif
		}
		else
		{
			#ifdef DEBUGCOLORS
			fprintf(logfile, "FillCurrentPalette: schemecolor");
			fflush(logfile);
			#endif
			FxU32 rbase = COLOR_R(schemecolor);
			FxU32 gbase = COLOR_G(schemecolor);
			FxU32 bbase = COLOR_B(schemecolor);
			for (i=0; i<256; i++)
			{
				c = texturepalette.data[i];
				currentpalette[i] = (((rbase * COLOR_R(c)) >> (16+4)) << 5)
				                  | (((gbase * COLOR_G(c)) >> (16+4)) << 9)
				                  | (((bbase * COLOR_B(c)) >> (16+5)) << 13);
				#ifdef DEBUGCOLORS
				fprintf(logfile, "  %08x", currentpalette[i]);
				fflush(logfile);
				#endif
			}
			#ifdef DEBUGCOLORS
			fprintf(logfile, "\n");
			fflush(logfile);
			#endif
		}
	}
	currentpaletteok = 1;
}

//Since we do integer multiplication with colors, we have to shift the color-values back to normal
#define fsc2c_macro(f,s,c) (fc2c_macro((f),((s)*(c)) >> 8))
#define fc2c_macro(f,c) (((f) + ((255 - fogValue) * (c))) >> 8)

void BuildFullPalette(void)
{
	GrFog_t fogValue;
	unsigned int maxFogEntries;
	unsigned int i,j,s;
	unsigned int cs; //Number of colors
	unsigned int schemecolor[SOLIDCOLORSCHEMES][3];
	unsigned int fogr, fogg, fogb;

	#ifdef DEBUGCALLS
	fprintf(logfile, "BuildFullPalette()\n");
	fflush(logfile);
	#endif

	#ifdef DEBUGCOLORS
	fprintf(logfile, "unifiedpalettemode: %d\n", unifiedpalettemode);
	fflush(logfile);
	fprintf(logfile, "colormode: %d\n", colormode);
	fflush(logfile);
	fprintf(logfile, "enableFog: %d\n", enableFog);
	fflush(logfile);
	#endif

	if (unifiedpalettemode)
	{
		if (colormode & GR_COLORCOMBINE_TEXTURE)
			cs = COLORSCHEMES;
		else
		{
			cs = SOLIDCOLORSCHEMES;
		}
		for (s = 0; s < cs; s++)
		{
			schemecolor[s][0] = COLOR_R(SchemeBaseColor[s]);
			schemecolor[s][1] = COLOR_G(SchemeBaseColor[s]);
			schemecolor[s][2] = COLOR_B(SchemeBaseColor[s]);
		}
	}

	if (enableFog)
	{
		maxFogEntries = GR_FOG_TABLE_SIZE;
	}
	else
	{
		maxFogEntries = 1;
	}
	fullpalette = (FxU32*)malloc(sizeof(FxU32) * (1 << 16) * maxFogEntries); //FIXME: 1 << 16: NO! We're using fewer bits!
	if (!fullpalette)
	{
		abort();
	}

	for (j = 0; j < maxFogEntries; j++)
	{
		unsigned int base = j * (1 << 16);
		#ifdef DEBUGCOLORS
		fprintf(logfile, "Now doing fog entry: %d\n", j);
		fflush(logfile);
		#endif
		if (enableFog)
		{
			fogValue = fogtable[j];
		}
		else
		{
			fogValue = 0;
		}

		fogr = fogValue * COLOR_R(fogcolor);
		fogg = fogValue * COLOR_G(fogcolor);
		fogb = fogValue * COLOR_B(fogcolor);

		#ifdef DEBUGFOG
		fprintf(logfile, "Fog: %d: %u\n", j, fogValue);
		fflush(logfile);
		#endif

		if (unifiedpalettemode)
		{
			if (colormode & GR_COLORCOMBINE_TEXTURE)
			{
				#ifdef DEBUGCOLORS
				fprintf(logfile, "BuildFullPalette: Unified palette AND GR_COLORCOMBINE_TEXTURE");
				fflush(logfile);
				#endif
				for (i = 0; i < 256; i++)
				{
					FxU32 c = texturepalette.data[i];
					for (s = 0; s < COLORSCHEMES; s++)
					{
						fullpalette[base] = (fsc2c_macro(fogr, schemecolor[s][0], COLOR_R(c)) << 16)
						                  | (fsc2c_macro(fogg, schemecolor[s][1], COLOR_G(c)) << 8)
						                  | (fsc2c_macro(fogb, schemecolor[s][2], COLOR_B(c)));
						#ifdef DEBUGCOLORS
						fprintf(logfile, "  %06x", fullpalette[base]);
						fflush(logfile);
						#endif
						base += (1 << 5);
					}
				}
				#ifdef DEBUGCOLORS
				fprintf(logfile, "\n");
				fflush(logfile);
				#endif
			}
			else
			{
				unsigned int base = j * (1 << 16);
				#ifdef DEBUGCOLORS
				fprintf(logfile, "BuildFullPalette: Unified palette AND NOT GR_COLORCOMBINE_TEXTURE");
				fflush(logfile);
				#endif
				for (s = 0; s < SOLIDCOLORSCHEMES; s++)
				{
					fullpalette[base] = ((fc2c_macro(fogr, schemecolor[s][0])) << 16)
					                  | ((fc2c_macro(fogg, schemecolor[s][1])) << 8)
					                  | ((fc2c_macro(fogb, schemecolor[s][2])));
					#ifdef DEBUGCOLORS
					fprintf(logfile, "  %06x", fullpalette[base]);
					fflush(logfile);
					#endif
					base += (1 << 8);
				}
				#ifdef DEBUGCOLORS
				fprintf(logfile, "\n");
				fflush(logfile);
				#endif
			}
		}
		else
		{
			unsigned int rr, gg, bb;
			int rcount, gcount, bcount;

			#ifdef DEBUGCOLORS
			fprintf(logfile, "BuildFullPalette: NOT Unified palette");
			fflush(logfile);
			#endif
			for (bcount = 0; bcount < (1 << 3); bcount++)
			{
				bb = ((255 * bcount) / ((1 << 3) - 1));
				if (bb > 255) bb = 255;
				for (gcount = 0; gcount < (1 << 4); gcount++)
				{
					gg = ((255 * gcount) / ((1 << 4) - 1));
					if (gg > 255) gg = 255;
					for (rcount = 0; rcount < (1 << 4); rcount++)
					{
						rr = ((255 * rcount) / ((1 << 4) - 1));
						if (rr > 255) rr = 255;
						fullpalette[base] = ((fc2c_macro(fogr, rr)) << 16)
						                  | ((fc2c_macro(fogg, gg)) << 8)
						                  | ((fc2c_macro(fogb, bb)));
						#ifdef DEBUGCOLORS
						fprintf(logfile, "  %06x", fullpalette[base]);
						fflush(logfile);
						#endif
						base += (1 << 5);
					}
				}
			}
			#ifdef DEBUGCOLORS
			fprintf(logfile, "\n");
			fflush(logfile);
			#endif
		}
	}

	#ifdef DEBUGCOLORS
	fprintf(logfile, "Full palette constructed!\n");
	fflush(logfile);
	#endif
}

void FreeFullPalette(void)
{
	#ifdef DEBUGCALLS
	fprintf(logfile, "FreeFullPalette()\n");
	fflush(logfile);
	#endif

	if (fullpalette)
	{
		free(fullpalette);
		fullpalette = 0;
	}
}

int __stdcall softgQuArK(void)
{
	#if defined(DEBUG) | defined(DEBUGCALLS) | defined(DEBUGCOLORS) | defined(DEBUGOOW) | defined(DEBUGFOG)
	#pragma warning(suppress : 4996)
	logfile = fopen(logfilename, "w");
	#endif

	#ifdef DEBUGCALLS
	fprintf(logfile, "softgQuArK()\n");
	fflush(logfile);
	#endif

	return SOFTG_QUARK_VERSION_NUMBER;
}

void setschemecolor(void)
{
	unsigned int i, j;
	static FxU32 time = 0;
	FxU32 mintime, color;

	#ifdef DEBUGCALLS
	fprintf(logfile, "setschemecolor()\n");
	fflush(logfile);
	#endif

	if (colormode & GR_COLORCOMBINE_CCRGB)
		color = schemecolor;
	else
		color = 0xFFFFFF;
	if (colormode & GR_COLORCOMBINE_TEXTURE)
	{
		if (oow_table_mode != GR_COLORCOMBINE_TEXTURE)
		{
			oow_table_mode = GR_COLORCOMBINE_TEXTURE;
		}
		for (i=0; i<COLORSCHEMES; i++)
			if (color == SchemeBaseColor[i])
			{
				scheme = i<<(8-COLORSCHEMEBITS);
				SchemesUsageTime[i] = ++time;
				return;
			}
		mintime = ++time;
		for (i=0; i<COLORSCHEMES; i++)
			if (SchemesUsageTime[i] < mintime)
			{
				scheme = i;
				mintime = SchemesUsageTime[i];
			}
		SchemesUsageTime[scheme] = time;
		SchemeBaseColor[scheme] = color;
		scheme <<= 8-COLORSCHEMEBITS;
	}
	else
	{
		if (oow_table_mode != GR_COLORCOMBINE_CCRGB)
		{
			oow_table_mode = GR_COLORCOMBINE_CCRGB;
		}
		for (j=0; j<SOLIDCOLORSCHEMES/2; j++)
			if (color == SchemeBaseColor[i = ((time-j) & (SOLIDCOLORSCHEMES-1))])
			{
				scheme = i<<8;
				return;
			}
		scheme = (++time) & (SOLIDCOLORSCHEMES-1);
		SchemeBaseColor[scheme] = color;
		scheme <<= 8;
	}
	FreeFullPalette();
}

void __stdcall grConstantColorValue(GrColor_t color)
{
	#ifdef DEBUGCALLS
	fprintf(logfile, "grConstantColorValue(%u)\n", color);
	fflush(logfile);
	#endif

	schemecolor = color & 0xFFFFFF;
	if (unifiedpalettemode)
		setschemecolor();
	else
	{
		scheme = PACKCOLOR(schemecolor);
		currentpaletteok = 0;
	}
}

void __stdcall guColorCombineFunction(GrColorCombineFunction_t func)
{
	#ifdef DEBUGCALLS
	fprintf(logfile, "guColorCombineFunction(%d)\n", func);
	fflush(logfile);
	#endif

	if (unifiedpalettemode)
	{
		if ((colormode^func) & GR_COLORCOMBINE_TEXTURE)
			FreeFullPalette();
		colormode = func;
		setschemecolor();
	}
	else
		colormode = func;
}


void __stdcall grHints(GrHint_t type, FxU32 hintMask)
{
	#ifdef DEBUGCALLS
	fprintf(logfile, "grHints(%u, %u)\n", type, hintMask);
	fflush(logfile);
	#endif

	if (type == GR_HINT_STWHINT)
	{
		hintMask &= GR_STWHINT_W_DIFF_TMU0;
		if (hintMask!=flatdisplay)
		{
			flatdisplay = hintMask;
			FreeFullPalette();
		}
		// Note: in the GR_STWHINT_W_DIFF_TMU0 case,
		// qrksoftg assumes that tmuvtx[0].oow == 1 for all vertices (flat display)
	}
}

void __stdcall grTexSource(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info)
{
	int texwbits, texhbits;
	int size1=8-info->largeLod;

	#ifdef DEBUGCALLS
	fprintf(logfile, "grTexSource(%d, %u, %u, %p)\n", tmu, startAddress, evenOdd, info);
	fflush(logfile);
	#endif

	#ifdef DEBUG
	fprintf(logfile, "grTexSource: format: %d\n", info->format);
	fflush(logfile);
	#endif

	//texsource=info;
	texdata = (FxU8*) info->data;
	if (info->aspectRatio<=3)
		texwbits=size1;
	else
		texwbits=size1-(info->aspectRatio-3);
	texh1 = texwbits;
	texwmask = (1 << texwbits) - 1;
	if (info->aspectRatio>=3)
		texhbits = size1;
	else
		texhbits = size1 - (3 - info->aspectRatio);
	texhmask = (1 << texhbits) - 1;
	stowbase = 1.0f / (256>>size1);
	#ifdef DEBUG
	fprintf(logfile, "texwbits=%d   texwmask=%d   texhbits=%d   texhmask=%d\n", texwbits, texwmask, texhbits, texhmask);
	fflush(logfile);
	#endif

	if (info->format == GR_TEXFMT_RGB_565)
	{   // in-place convertion to internal format
		FxU16 *p = (FxU16*)texdata;
		FxU16 *end = p + (1 << (texwbits + texhbits));
		while (p<end)
		{
			FxU16 value = *p;
			*p++ = ((value << 11)&0xE000) | ((value & 0x0780) << 2) | ((value & 0xF000) >> 7);
		}
		info->format = GR_TEXFMT_RGB_443;
	}
	if (info->format == GR_TEXFMT_RGB_443)
		texture_mode = softgRGB443Texture;
	else
		texture_mode = 0;
}


void setunifiedpalette(unsigned int n)
{
	#ifdef DEBUGCALLS
	fprintf(logfile, "setunifiedpalette(%u)\n", n);
	fflush(logfile);
	#endif

	unifiedpalettemode = n;
	schemecolor = 0xFFFFFFu;
	if (unifiedpalettemode)
	{
		unsigned int i;
		oow_table_mode = 0;
		for (i=0; i<SOLIDCOLORSCHEMES; i++)
			SchemeBaseColor[i] = 0xFFFFFFu;
		memset(SchemesUsageTime, 0, sizeof(SchemesUsageTime));
		colormode = GR_COLORCOMBINE_TEXTURE;
		setschemecolor();
	}
	else
	{
		scheme = PACKCOLOR(0xFFFFFFu);
		FreeFullPalette();
	}
}

//Inverse of: guFogTableIndexToW
unsigned int fogTableIndexForW(float w)
{
	if (w <= 1.0f)
	{
		return 0;
	}
	return (unsigned int)(4.0f * log2f(w));
}

FxU8 fogValueForW(float w)
{
	unsigned i;
	float wprev, wnext;

	#ifdef DEBUGFOG
	fprintf(logfile, "fogValueForW(%f)\n", w);
	fflush(logfile);
	#endif

	if (!enableFog)
		return 0;

	i = fogTableIndexForW(w);
	#ifdef DEBUGFOG
	fprintf(logfile, "fogValueForW: raw i: %d\n", i);
	fflush(logfile);
	#endif
	if (i >= GR_FOG_TABLE_SIZE - 1)
	{
		return fogtable[GR_FOG_TABLE_SIZE - 1];
	}

	#ifdef DEBUGFOG
	fprintf(logfile, "fogValueForW: clamp i: %d\n", i);
	fflush(logfile);
	#endif

	wprev = guFogTableIndexToW(i);
	wnext = guFogTableIndexToW(i + 1);
	#ifdef DEBUGFOG
	fprintf(logfile, "fogValueForW: Ws: %f, %f\n", wprev, wnext);
	fflush(logfile);
	#endif
	return (((w - wprev) * fogtable[i]) + ((wnext - w) * fogtable[i + 1])) / (wnext - wprev);
}

//Calculated the index into the full palette based on the (p)alette index and the (f)og value
#define fp_macro(f, p) ((256*(unsigned int)(f))+(p))
#define pixel_macro(i) (fp_macro(fogValueForW(1.0f / depthbuffer[i]), framebuffer[i]))

void __stdcall softgLoadFrameBuffer(int *buffer, int format)
{
  unsigned int i, end;
  int j, bufferline;
  FxU32 c1, c2, c3, c4, c5, c6, c7, c8, c9, c10;

  #ifdef DEBUGCALLS
  fprintf(logfile, "softgLoadFrameBuffer(%p, %d)\n", buffer, format);
  fflush(logfile);
  #endif

  if (!buffer)
  {
    if (format & softg16BitColor)
    {
      format &= softgUnifiedPalette;
      if (unifiedpalettemode!=format)
        setunifiedpalette(format);
    }
    return;
  }

  if (!fullpalette)
    BuildFullPalette();
  switch (format)
  {
    case 0:   // pixel-by-pixel copying
    {
      for (i=0; i<framecount; i+=4)
      {
        c1 = fullpalette[pixel_macro(i)];
        c2 = fullpalette[pixel_macro(i+1)];
        *(buffer++) = c1 | (c2<<24);
        c3 = fullpalette[pixel_macro(i+2)];
        *(buffer++) = (c2>>8) | (c3<<16);
        c4 = fullpalette[pixel_macro(i+3)];
        *(buffer++) = (c3>>16) | (c4<<8);
      }
      break;
    }
    case 1:   // expand each pixel into a 2x2 square
    {
      bufferline = framew*3/2;
      j = framew/2;
      for (i=0; i<framecount; i+=2)
      {
        c1 = fullpalette[pixel_macro(i)];
        buffer[bufferline] = buffer[0] = c1 | (c1<<24);
        c2 = fullpalette[pixel_macro(i+1)];
        buffer[bufferline+1] = buffer[1] = (c1>>8) | (c2<<16);
        buffer[bufferline+2] = buffer[2] = (c2>>16) | (c2<<8);
        buffer+=3;
        if (!--j)
        {
          buffer+=bufferline;
          j = framew/2;
        }
      }
      break;
    }
    case 2:   // interpolate pixels to create an image twice as big
    {
      bufferline = framew*3/2;
      j = framew/2;
      end = framecount-framew;
      c5 = c6 = 0;
      for (i=2; i<end; i+=2)
      {
        c1 = c5;                                   //   ...........   
        c2 = c6;                                   //   c1 c7 c3 c8 c5 .
        c3 = fullpalette[pixel_macro(i-1)];        //  c10 c7 c9 c8    .
        c5 = fullpalette[pixel_macro(i)];          //   c2    c4    c6
        c7 = ((c1&0xFEFEFE)+(c3&0xFEFEFE))/2;
        c8 = ((c3&0xFEFEFE)+(c5&0xFEFEFE))/2;
        buffer[0] = c1 | (c7<<24);
        buffer[1] = (c7>>8) | (c3<<16);
        buffer[2] = (c3>>16) | (c8<<8);
        c4 = fullpalette[pixel_macro(i+framew-1)];
        c6 = fullpalette[pixel_macro(i+framew)];
        c7 = ((c1&0xFCFCFC)+(c2&0xFCFCFC)+(c3&0xFCFCFC)+(c4&0xFCFCFC))/4;
        c8 = ((c5&0xFCFCFC)+(c6&0xFCFCFC)+(c3&0xFCFCFC)+(c4&0xFCFCFC))/4;
        c9 = ((c3&0xFEFEFE)+(c4&0xFEFEFE))/2;
        c10 = ((c1&0xFEFEFE)+(c2&0xFEFEFE))/2;
        buffer[bufferline] = c10 | (c7<<24);
        buffer[bufferline+1] = (c7>>8) | (c9<<16);
        buffer[bufferline+2] = (c9>>16) | (c8<<8);
        buffer+=3;
        if (!--j)
        {
          buffer+=bufferline;
          j = framew/2;
        }
      }
      break;
    }
  }
}

#define VERYSMALL(value)  ((value)<EPSILON && (value)>-EPSILON)

void __stdcall grDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
  const GrVertex *d, *a2, *b2, *c2;
  GrTmuVertex deltah, deltav, cur, cur2;
  int scanline, midline, lastline, curx, minx, maxx;
  int padright, curx2, s, t, i;
  float temp, temp1, curhx, curvy, left, right, left1, right1;
  FxU16 *dest;
  float *dest_depth;

  #ifdef DEBUGCALLS
  fprintf(logfile, "grDrawTriangle(%p, %p, %p)\n", a, b, c);
  fflush(logfile);
  #endif

  if (a->y > b->y) {d=a; a=b; b=d;}
  if (a->y > c->y) {d=a; a=c; c=d;}
  if (b->y > c->y) {d=b; b=c; c=d;}

  scanline=(int)a->y + 1;
  lastline=(int)c->y;
  if (scanline>lastline) return;
  midline =(int)b->y;

  temp1 = 1.0f / (c->y - a->y);
  temp = (b->y - a->y) * temp1;
  curhx = a->x + (c->x - a->x) * temp;
  #ifdef DEBUG
  fprintf(logfile, "scanline %d   midline %d   lastline %d   curhx %f\n", scanline, midline, lastline, curhx);
  fflush(logfile);
  #endif
  if (VERYSMALL(b->x-curhx)) return;
  if (b->x>curhx)
  {
    left1 = (c->x - a->x) * temp1;
    if (scanline<=midline)
      right1 = (b->x - a->x) / (b->y - a->y);
  }
  else
  {
    right1 = (c->x - a->x) * temp1;
    if (scanline<=midline)
      left1 = (b->x - a->x) / (b->y - a->y);
  }

  #ifdef DEBUGOOW
  fprintf(logfile, "a->oow=%f   b->oow=%f   c->oow=%f\n", a->oow, b->oow, c->oow);
  fflush(logfile);
  #endif

  #ifdef DEBUG
  fprintf(logfile, "colormode %d\n", colormode);
  fflush(logfile);
  #endif

  if (colormode & GR_COLORCOMBINE_TEXTURE)
  {
    unsigned int displayroutines = unifiedpalettemode | flatdisplay | texture_mode;
    #ifdef DEBUG
    fprintf(logfile, "displayroutines %d\n", displayroutines);
    fprintf(logfile, "%u %u %u\n", unifiedpalettemode, flatdisplay, texture_mode);
    fflush(logfile);
    #endif

    if (!texture_mode)
      if (!currentpaletteok)
        FillCurrentPalette();

    deltah.sow = a->tmuvtx[0].sow + (c->tmuvtx[0].sow - a->tmuvtx[0].sow) * temp - b->tmuvtx[0].sow;
    deltah.tow = a->tmuvtx[0].tow + (c->tmuvtx[0].tow - a->tmuvtx[0].tow) * temp - b->tmuvtx[0].tow;
    deltah.oow =           a->oow + (          c->oow -           a->oow) * temp -           b->oow;
    temp = 1.0f / (curhx - b->x);
    deltah.sow *= temp * stowbase;
    deltah.tow *= temp * stowbase;
    deltah.oow *= temp * OOWTABLESIZE;

    a2=a;
    b2=b;
    c2=c;
    if (a2->x > b2->x) {d=a2; a2=b2; b2=d;}
    if (a2->x > c2->x) {d=a2; a2=c2; c2=d;}
    if (b2->x > c2->x) {d=b2; b2=c2; c2=d;}

    temp = (b2->x - a2->x) / (c2->x - a2->x);
    curvy = a2->y + (c2->y - a2->y) * temp;
    if (VERYSMALL(b2->y-curvy)) return;
    deltav.sow = a2->tmuvtx[0].sow + (c2->tmuvtx[0].sow - a2->tmuvtx[0].sow) * temp - b2->tmuvtx[0].sow;
    deltav.tow = a2->tmuvtx[0].tow + (c2->tmuvtx[0].tow - a2->tmuvtx[0].tow) * temp - b2->tmuvtx[0].tow;
    deltav.oow =           a2->oow + (          c2->oow -           a2->oow) * temp -           b2->oow;
    temp = 1.0f / (curvy - b2->y);
    deltav.sow *= temp * stowbase;
    deltav.tow *= temp * stowbase;
    deltav.oow *= temp * OOWTABLESIZE;

    curx = (int)a->x;
    temp = curx - a->x;
    temp1 = scanline - a->y;
    cur.sow = a->tmuvtx[0].sow*stowbase + temp*deltah.sow + temp1*deltav.sow;
    cur.tow = a->tmuvtx[0].tow*stowbase + temp*deltah.tow + temp1*deltav.tow;
    cur.oow = a->oow*OOWTABLESIZE           + temp*deltah.oow + temp1*deltav.oow;
    #ifdef DEBUGOOW
    fprintf(logfile, "cur.oow=%f   deltah.oow=%f   deltav.oow=%f\n", cur.oow, deltah.oow, deltav.oow);
    fflush(logfile);
    #endif
    if (scanline<=midline)
    {
      left = a->x + temp1*left1 + 0.999f;
      right = a->x + temp1*right1;
    }

    dest = framebuffer + (scanline - firstrow) * framew - firstcol;
    dest_depth = depthbuffer + (scanline - firstrow) * framew - firstcol;
    padright = c->x > a->x;
    while (1)
    {
      if (scanline>midline)
      {
        midline = lastline;
        temp1 = scanline - b->y;
        if (b->x>curhx)
        {
          right1 = (c->x - b->x) / (c->y - b->y);
          left = curhx + temp1*left1 + 0.999f;
          right = b->x + temp1*right1;
        }
        else
        {
          left1 = (c->x - b->x) / (c->y - b->y);
          #ifdef DEBUG
          fprintf(logfile, "b %f %f    c %f %f    left1 %f\n", b->x, b->y, c->x, c->y, left1);
          fflush(logfile);
          #endif
          left = b->x + temp1*left1 + 0.999f;
          right = curhx + temp1*right1;
        }
      }

      minx=(int)left;
      maxx=(int)right;
      if (minx<=maxx)
      {
        #ifdef DEBUG
        fprintf(logfile, "%d - %d\n", minx, maxx);
        fflush(logfile);
        #endif
        for (; curx<minx; curx++)
        {
          cur.sow+=deltah.sow;
          cur.tow+=deltah.tow;
          cur.oow+=deltah.oow;
        }
        for (; curx>maxx; curx--)
        {
          cur.sow-=deltah.sow;
          cur.tow-=deltah.tow;
          cur.oow-=deltah.oow;
        }
        cur2.sow = cur.sow;
        cur2.tow = cur.tow;
        cur2.oow = cur.oow;
        curx2 = curx;
        switch (displayroutines)
        {
        case softgRGB443Texture | softgFlatDisplay:
          while (1)
          {
            i = (int)cur.oow;
            if (i>=OOWTABLESIZE) i=OOWTABLESIZE-1;
            else if (i<0) i=0;
            #ifdef DEBUGOOW
            fprintf(logfile, "oow index: %d\n", i);
            fflush(logfile);
            #endif
            if (cur.oow > dest_depth[curx])
            {
              s = (int)cur.sow;
              t = (int)cur.tow;
              dest[curx] = ((FxU16*)texdata)[(s&texwmask) | ((t&texhmask)<<texh1)];
              dest_depth[curx] = cur.oow;
            }
            if (curx==minx) break;
            curx--;
            cur.sow-=deltah.sow;
            cur.tow-=deltah.tow;
            cur.oow-=deltah.oow;
          }
          while (curx2<maxx)
          {
            curx2++;
            cur2.sow+=deltah.sow;
            cur2.tow+=deltah.tow;
            cur2.oow+=deltah.oow;

            i = (int)cur2.oow;
            if (i>=OOWTABLESIZE) i=OOWTABLESIZE-1;
            else if (i<0) i=0;
            #ifdef DEBUGOOW
            fprintf(logfile, "oow index: %d\n", i);
            fflush(logfile);
            #endif
            if (cur2.oow > dest_depth[curx2])
            {
              s = (int)cur2.sow;
              t = (int)cur2.tow;
              dest[curx2] = ((FxU16*)texdata)[(s&texwmask) | ((t&texhmask)<<texh1)];
              dest_depth[curx2] = cur2.oow;
            }
          }
          break;
        case softgRGB443Texture:
          while (1)
          {
            i = (int)cur.oow;
            if (i>=OOWTABLESIZE) i=OOWTABLESIZE-1;
            else if (i<0) i=0;
            #ifdef DEBUGOOW
            fprintf(logfile, "oow index: %d\n", i);
            fflush(logfile);
            #endif
            if (cur.oow > dest_depth[curx])
            {
              temp = (1.0f / cur.oow) * OOWTABLESIZE;
              s = (int)(cur.sow*temp);
              t = (int)(cur.tow*temp);
              dest[curx] = ((FxU16*)texdata)[(s & texwmask) | ((t & texhmask) << texh1)];
              dest_depth[curx] = cur.oow;
            }
            if (curx==minx) break;
            curx--;
            cur.sow-=deltah.sow;
            cur.tow-=deltah.tow;
            cur.oow-=deltah.oow;
          }
          while (curx2<maxx)
          {
            curx2++;
            cur2.sow+=deltah.sow;
            cur2.tow+=deltah.tow;
            cur2.oow+=deltah.oow;

            i = (int)cur2.oow;
            if (i>=OOWTABLESIZE) i=OOWTABLESIZE-1;
            else if (i<0) i=0;
            #ifdef DEBUGOOW
            fprintf(logfile, "oow index: %d\n", i);
            fflush(logfile);
            #endif
            if (cur2.oow > dest_depth[curx2])
            {
              temp = (1.0f / cur2.oow) * OOWTABLESIZE;
              s = (int)(cur2.sow*temp);
              t = (int)(cur2.tow*temp);
              dest[curx2] = ((FxU16*)texdata)[(s&texwmask) | ((t&texhmask)<<texh1)];
              dest_depth[curx2] = cur2.oow;
            }
          }
          break;
        case softgFlatDisplay:
          while (1)
          {
            i = (int)cur.oow;
            if (i>=OOWTABLESIZE) i=OOWTABLESIZE-1;
            else if (i<0) i=0;
            #ifdef DEBUGOOW
            fprintf(logfile, "oow index: %d\n", i);
            fflush(logfile);
            #endif
            if (cur.oow > dest_depth[curx])
            {
              s = (int)cur.sow;
              t = (int)cur.tow;
              dest[curx] = currentpalette[texdata[(s & texwmask) | ((t & texhmask) << texh1)]];
              dest_depth[curx] = cur.oow;
            }
            if (curx==minx) break;
            curx--;
            cur.sow-=deltah.sow;
            cur.tow-=deltah.tow;
            cur.oow-=deltah.oow;
          }
          while (curx2<maxx)
          {
            curx2++;
            cur2.sow+=deltah.sow;
            cur2.tow+=deltah.tow;
            cur2.oow+=deltah.oow;

            i = (int)cur2.oow;
            if (i>=OOWTABLESIZE) i=OOWTABLESIZE-1;
            else if (i<0) i=0;
            #ifdef DEBUGOOW
            fprintf(logfile, "oow index: %d\n", i);
            fflush(logfile);
            #endif
            if (cur2.oow > dest_depth[curx2])
            {
              s = (int)cur2.sow;
              t = (int)cur2.tow;
              dest[curx2] = currentpalette[texdata[(s & texwmask) | ((t & texhmask) << texh1)]];
              dest_depth[curx2] = cur2.oow;
            }
          }
          break;
        case 0:
          while (1)
          {
            i = (int)cur.oow;
            if (i>=OOWTABLESIZE) i=OOWTABLESIZE-1;
            else if (i<0) i=0;
            #ifdef DEBUGOOW
            fprintf(logfile, "oow index: %d\n", i);
            fflush(logfile);
            #endif
            if (cur.oow > dest_depth[curx])
            {
              temp = (1.0f / cur.oow) * OOWTABLESIZE;
              s = (int)(cur.sow*temp);
              t = (int)(cur.tow*temp);
              dest[curx] = currentpalette[texdata[(s & texwmask) | ((t & texhmask) << texh1)]];
              dest_depth[curx] = cur.oow;
            }
            if (curx==minx) break;
            curx--;
            cur.sow-=deltah.sow;
            cur.tow-=deltah.tow;
            cur.oow-=deltah.oow;
          }
          while (curx2<maxx)
          {
            curx2++;
            cur2.sow+=deltah.sow;
            cur2.tow+=deltah.tow;
            cur2.oow+=deltah.oow;

            i = (int)cur2.oow;
            if (i>=OOWTABLESIZE) i=OOWTABLESIZE-1;
            else if (i<0) i=0;
            #ifdef DEBUGOOW
            fprintf(logfile, "oow index: %d\n", i);
            fflush(logfile);
            #endif
            if (cur2.oow > dest_depth[curx2])
            {
              temp = (1.0f / cur2.oow) * OOWTABLESIZE;
              s = (int)(cur2.sow*temp);
              t = (int)(cur2.tow*temp);
              dest[curx2] = currentpalette[texdata[(s&texwmask) | ((t&texhmask)<<texh1)]];
              dest_depth[curx2] = cur2.oow;
            }
          }
          break;
        case softgFlatDisplay | softgUnifiedPalette:
          while (1)
          {
            i = (int)cur.oow;
            if (i>=OOWTABLESIZE) i=OOWTABLESIZE-1;
            else if (i<0) i=0;
            #ifdef DEBUGOOW
            fprintf(logfile, "oow index: %d\n", i);
            fflush(logfile);
            #endif
            if (cur.oow > dest[curx])
            {
              s = (int)cur.sow;
              t = (int)cur.tow;
              dest[curx] = scheme | (((FxU16)texdata[(s & texwmask) | ((t & texhmask) << texh1)]) << 8);
              dest_depth[curx] = cur.oow;
            }
            if (curx==minx) break;
            curx--;
            cur.sow-=deltah.sow;
            cur.tow-=deltah.tow;
            cur.oow-=deltah.oow;
          }
          while (curx2<maxx)
          {
            curx2++;
            cur2.sow+=deltah.sow;
            cur2.tow+=deltah.tow;
            cur2.oow+=deltah.oow;

            i = (int)cur2.oow;
            if (i>=OOWTABLESIZE) i=OOWTABLESIZE-1;
            else if (i<0) i=0;
            #ifdef DEBUGOOW
            fprintf(logfile, "oow index: %d\n", i);
            fflush(logfile);
            #endif
            if (cur2.oow > dest_depth[curx2])
            {
              s = (int)cur2.sow;
              t = (int)cur2.tow;
              dest[curx2] = scheme | (((FxU16)texdata[(s&texwmask) | ((t&texhmask)<<texh1)])<<8);
              dest_depth[curx2] = cur2.oow;
            }
          }
          break;
        case softgUnifiedPalette:
          while (1)
          {
            i = (int)cur.oow;
            if (i>=OOWTABLESIZE) i=OOWTABLESIZE-1;
            else if (i<0) i=0;
            #ifdef DEBUGOOW
            fprintf(logfile, "oow index: %d\n", i);
            fflush(logfile);
            #endif
            if (cur.oow > dest_depth[curx])
            {
              temp = (1.0f / cur.oow) * OOWTABLESIZE;
              s = (int)(cur.sow*temp);
              t = (int)(cur.tow*temp);
              dest[curx] = scheme | (((FxU16)texdata[(s&texwmask) | ((t&texhmask)<<texh1)])<<8);
              dest_depth[curx] = cur.oow;
            }
            if (curx==minx) break;
            curx--;
            cur.sow-=deltah.sow;
            cur.tow-=deltah.tow;
            cur.oow-=deltah.oow;
          }
          while (curx2<maxx)
          {
            curx2++;
            cur2.sow+=deltah.sow;
            cur2.tow+=deltah.tow;
            cur2.oow+=deltah.oow;

            i = (int)cur2.oow;
            if (i>=OOWTABLESIZE) i=OOWTABLESIZE-1;
            else if (i<0) i=0;
            #ifdef DEBUGOOW
            fprintf(logfile, "oow index: %d\n", i);
            fflush(logfile);
            #endif
            if (cur2.oow > dest_depth[curx2])
            {
              temp = (1.0f / cur2.oow) * OOWTABLESIZE;
              s = (int)(cur2.sow*temp);
              t = (int)(cur2.tow*temp);
              dest[curx2] = scheme | (((FxU16)texdata[(s&texwmask) | ((t&texhmask)<<texh1)])<<8);
              dest_depth[curx2] = cur2.oow;
            }
          }
          break;
        }
      }

      if (++scanline>lastline) break;
      left+=left1;
      right+=right1;
      if (padright && (minx<=maxx))
      {
        curx = curx2;
        cur.sow = cur2.sow+deltav.sow;
        cur.tow = cur2.tow+deltav.tow;
        cur.oow = cur2.oow+deltav.oow;
      }
      else
      {
        cur.sow+=deltav.sow;
        cur.tow+=deltav.tow;
        cur.oow+=deltav.oow;
      }
      dest+=framew;
      dest_depth+=framew;
    }
  }
  else      // colormode & GR_COLORCOMBINE_TEXTURE == 0
  {
    deltah.oow =           a->oow + (          c->oow -           a->oow) * temp -           b->oow;
    temp = 1.0f / (curhx - b->x);
    deltah.oow *= temp * OOWTABLESIZE;

    a2=a;
    b2=b;
    c2=c;
    if (a2->x > b2->x) {d=a2; a2=b2; b2=d;}
    if (a2->x > c2->x) {d=a2; a2=c2; c2=d;}
    if (b2->x > c2->x) {d=b2; b2=c2; c2=d;}

    temp = (b2->x - a2->x) / (c2->x - a2->x);
    curvy = a2->y + (c2->y - a2->y) * temp;
    if (VERYSMALL(b2->y-curvy)) return;
    deltav.oow =           a2->oow + (          c2->oow -           a2->oow) * temp -           b2->oow;
    temp = 1.0f / (curvy - b2->y);
    deltav.oow *= temp * OOWTABLESIZE;

    curx = (int)a->x;
    temp = curx - a->x;
    temp1 = scanline - a->y;
    cur.oow = a->oow*OOWTABLESIZE           + temp*deltah.oow + temp1*deltav.oow;
    #ifdef DEBUGOOW
    fprintf(logfile, "cur.oow=%f   deltah.oow=%f   deltav.oow=%f\n", cur.oow, deltah.oow, deltav.oow);
    fflush(logfile);
    #endif
    if (scanline<=midline)
    {
      left = a->x + temp1*left1 + 0.999f;
      right = a->x + temp1*right1;
    }

    dest = framebuffer + (scanline-firstrow)*framew - firstcol;
    dest_depth = depthbuffer + (scanline - firstrow) * framew - firstcol;
    padright = c->x > a->x;
    while (1)
    {
      if (scanline>midline)
      {
        midline = lastline;
        temp1 = scanline - b->y;
        if (b->x>curhx)
        {
          right1 = (c->x - b->x) / (c->y - b->y);
          left = curhx + temp1*left1 + 0.999f;
          right = b->x + temp1*right1;
        }
        else
        {
          left1 = (c->x - b->x) / (c->y - b->y);
          #ifdef DEBUG
          fprintf(logfile, "b %f %f    c %f %f    left1 %f\n", b->x, b->y, c->x, c->y, left1);
          fflush(logfile);
          #endif
          left = b->x + temp1*left1 + 0.999f;
          right = curhx + temp1*right1;
        }
      }

      minx=(int)left;
      maxx=(int)right;
      if (minx<=maxx)
      {
        #ifdef DEBUG
        fprintf(logfile, "%d - %d\n", minx, maxx);
        fflush(logfile);
        #endif
        for (; curx<minx; curx++)
        {
          cur.oow+=deltah.oow;
        }
        for (; curx>maxx; curx--)
        {
          cur.oow-=deltah.oow;
        }
        cur2.oow = cur.oow;
        curx2 = curx;
        while (1)
        {
          i = (int)cur.oow;
          if (i>=OOWTABLESIZE) i=OOWTABLESIZE-1;
          else if (i<0) i=0;
          #ifdef DEBUGOOW
          fprintf(logfile, "oow index: %d\n", i);
          fflush(logfile);
          #endif
          if (cur.oow > dest_depth[curx])
          {
            dest[curx] = scheme;
            dest_depth[curx] = cur.oow;
          }
          if (curx==minx) break;
          curx--;
          cur.oow-=deltah.oow;
        }
        while (curx2<maxx)
        {
          curx2++;
          cur2.oow+=deltah.oow;

          i = (int)cur2.oow;
          if (i>=OOWTABLESIZE) i=OOWTABLESIZE-1;
          else if (i<0) i=0;
          #ifdef DEBUGOOW
          fprintf(logfile, "oow index: %d\n", i);
          fflush(logfile);
          #endif
          if (cur2.oow > dest_depth[curx2])
          {
            dest[curx2] = scheme;
            dest_depth[curx2] = cur2.oow;
          }
        }
      }

      if (++scanline>lastline) break;
      left+=left1;
      right+=right1;
      if (padright && (minx<=maxx))
      {
        curx = curx2;
        cur.oow = cur2.oow+deltav.oow;
      }
      else
      {
        cur.oow+=deltav.oow;
      }
      dest+=framew;
      dest_depth+=framew;
    }
  }
}

void __stdcall grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU16 depth)
{
	unsigned int i;

	#ifdef DEBUGCALLS
	fprintf(logfile, "grBufferClear(%u, %u, %u)\n", color, alpha, depth);
	fflush(logfile);
	#endif

	memset(framebuffer, 0, framecount * sizeof(FxU16));
	for (i = 0; i < framecount; i++)
	{
		depthbuffer[i] = 1.0f / MAXW;
	}
}

void __stdcall grTexDownloadTable(GrChipID_t tmu, GrTexTable_t type, void *data)
{
	#ifdef DEBUGCALLS
	fprintf(logfile, "grTexDownloadTable(%d, %u, %p)\n", tmu, type, data);
	fflush(logfile);
	#endif

	memcpy(&texturepalette, (GuTexPalette*)data, sizeof(GuTexPalette));
	currentpaletteok = 0;
}

void __stdcall grGlideInit(void)
{
	#ifdef DEBUGCALLS
	fprintf(logfile, "grGlideInit()\n");
	fflush(logfile);
	#endif

	colormode = GR_COLORCOMBINE_TEXTURE;
	flatdisplay = 0;
	memset(&texturepalette, 0, sizeof(texturepalette));
	memset(currentpalette, 0, sizeof(currentpalette));
	setunifiedpalette(0);
}

void __stdcall grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy)
{
	#ifdef DEBUGCALLS
	fprintf(logfile, "grClipWindow(%u, %u, %u, %u)\n", minx, miny, maxx, maxy);
	fflush(logfile);
	#endif

	if (framebuffer)
	{
		free(framebuffer);
		framebuffer = 0;
	}
	if (depthbuffer)
	{
		free(depthbuffer);
		depthbuffer = 0;
	}
	framew = maxx-minx;
	frameh = maxy-miny;
	framecount = framew*frameh;
	firstcol = minx;
	firstrow = miny;
	framebuffer = (FxU16*)malloc(framecount * sizeof(FxU16));
	if (!framebuffer)
	{
		abort();
	}
	depthbuffer = (float*)malloc(framecount * sizeof(float));
	if (!depthbuffer)
	{
		abort();
	}
}

FxBool __stdcall grSstWinOpen(FxU32 hwnd, GrScreenResolution_t res, GrScreenRefresh_t ref, GrColorFormat_t cformat, GrOriginLocation_t org_loc, int num_buffers, int num_aux_buffers)
{
	#ifdef DEBUGCALLS
	fprintf(logfile, "grSstWinOpen(%u, %u, %u, %d, %d, %d, %d)\n", hwnd, res, ref, cformat, org_loc, num_buffers, num_aux_buffers);
	fflush(logfile);
	#endif

	grClipWindow(0, 0, 640, 480);
	return 1;
}

void __stdcall grSstWinClose(void)
{
	#ifdef DEBUGCALLS
	fprintf(logfile, "grSstWinClose()\n");
	fflush(logfile);
	#endif

	FreeFullPalette();
	free(framebuffer);
	framebuffer = 0;
	free(depthbuffer);
	depthbuffer = 0;
}

void __stdcall grFogMode(GrFogMode_t mode)
{
	#ifdef DEBUGCALLS
	fprintf(logfile, "grFogMode(%u)\n", mode);
	fflush(logfile);
	#endif

	if (mode == GR_FOG_WITH_TABLE)
	{
		if (enableFog == 0)
		{
			FreeFullPalette();
			enableFog = 1;
		}
	}
	else
	{
		if (enableFog == 1)
		{
			FreeFullPalette();
			enableFog = 0;
		}
	}
}

void __stdcall grFogColorValue(GrColor_t color)
{
	#ifdef DEBUGCALLS
	fprintf(logfile, "grFogColorValue(%u)\n", color);
	fflush(logfile);
	#endif

	FreeFullPalette();
	fogcolor = color & 0xFFFFFF;
}

void __stdcall guFogGenerateExp2(GrFog_t fogTable[GR_FOG_TABLE_SIZE], float density)
{
	unsigned int i;

	#ifdef DEBUGCALLS
	fprintf(logfile, "guFogGenerateExp2(%p, %f)\n", fogTable, density);
	fflush(logfile);
	#endif

	FreeFullPalette();
	for (i = 0; i < GR_FOG_TABLE_SIZE - 1; i++)
	{
		fogTable[i] = 255.0f * (1.0f - expf(-pow2f(density * guFogTableIndexToW(i))));
		#ifdef DEBUGFOG
		fprintf(logfile, "guFogGenerateExp2: Entry %u: %d\n", i, fogTable[i]);
		fflush(logfile);
		#endif
	}
	fogTable[GR_FOG_TABLE_SIZE - 1] = 255; //Last entry must be 255
}

/*void __stdcall guFogGenerateLinear(GrFog_t fogTable[GR_FOG_TABLE_SIZE], float nearW, float farW)
{
	unsigned int i;

	#ifdef DEBUGCALLS
	fprintf(logfile, "guFogGenerateLinear(%p, %f, %f)\n", table, nearW, farW);
	fflush(logfile);
	#endif

	FreeFullPalette();
	for (i = 0; i < GR_FOG_TABLE_SIZE; i++)
	{
		fogTable[i] = (guFogTableIndexToW(i) - nearW) / (farW - nearW);
		#ifdef DEBUGFOG
		fprintf(logfile, "guFogGenerateLinear: Entry %u: %d\n", i, fogTable[i]);
		fflush(logfile);
		#endif
	}
}*/

void __stdcall grFogTable(const GrFog_t table[GR_FOG_TABLE_SIZE])
{
	#ifdef DEBUGCALLS
	fprintf(logfile, "grFogTable(%p)\n", table);
	fflush(logfile);
	#endif

	FreeFullPalette();
	memcpy(fogtable, table, sizeof(fogtable));
}

float __stdcall guFogTableIndexToW(int i)
{
	#ifdef DEBUGCALLS
	fprintf(logfile, "guFogTableIndexToW(%d)\n", i);
	fflush(logfile);
	#endif

	//This is the official Glide prescription:
	//return pow(2.0, 3.0 + (double)(i >> 2)) / (8 - (i & 3));
	//But we're going with an approximation that makes our lives much easier:
	return powf(2.0f, (float)i / 4.0f);
}

/*
Convert GuPalette to currentpalette format:
for (i = 0; i < 256; i++)
{
	FxU32 value = ((GuTexPalette*)data)->data[i];
	currentpalette[i] = ((COLOR_R(value) & 0xF0) << 1) | ((COLOR_G(value) & 0xF0) << 5) | ((COLOR_B(value) & 0xE0) << 8);
}
*/
