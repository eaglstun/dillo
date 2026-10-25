/*
 * Dillo Widget — Apple (Core Graphics) image buffer.
 *
 * A port of dw/fltkimgbuf.cc: the root/scaled buffer machinery, gamma-aware
 * averaging scaler, and reference counting are unchanged; drawing goes
 * through a CGImage wrapped around the raw RGB rows instead of
 * fl_draw_image.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <math.h>
#include <string.h>

#include "../../lout/msg.h"
#include "../../lout/misc.hh"
#include "AppleImgbuf.hh"

#define IMAGE_MAX_AREA (6000 * 6000)

#define MAX_WIDTH 0x8000
#define MAX_HEIGHT 0x8000

namespace dw {
namespace apple {

using namespace lout::container::typed;

static const enum ScaleMode { SIMPLE, BEAUTIFUL, BEAUTIFUL_GAMMA }
   scaleMode = BEAUTIFUL_GAMMA;

Vector <AppleImgbuf::GammaCorrectionTable> *AppleImgbuf::gammaCorrectionTables
   = new Vector <AppleImgbuf::GammaCorrectionTable> (true, 2);

unsigned char *AppleImgbuf::findGammaCorrectionTable (double gamma)
{
   for (int i = 0; i < gammaCorrectionTables->size(); i++) {
      GammaCorrectionTable *gct = gammaCorrectionTables->get(i);
      if (gct->gamma == gamma)
         return gct->map;
   }

   GammaCorrectionTable *gct = new GammaCorrectionTable();
   gct->gamma = gamma;

   for (int i = 0; i < 256; i++)
      gct->map[i] = 255 * pow((double)i / 255, gamma);

   gammaCorrectionTables->put (gct);
   return gct->map;
}

bool AppleImgbuf::excessiveImageDimensions (int width, int height)
{
   return width <= 0 || height <= 0 ||
      width > IMAGE_MAX_AREA / height;
}

AppleImgbuf::AppleImgbuf (Type type, int width, int height, double gamma)
{
   init (type, width, height, gamma, NULL);
}

AppleImgbuf::AppleImgbuf (Type type, int width, int height, double gamma,
                          AppleImgbuf *root)
{
   init (type, width, height, gamma, root);
}

void AppleImgbuf::init (Type type, int width, int height, double gamma,
                        AppleImgbuf *root)
{
   if (excessiveImageDimensions (width, height)) {
      MSG("AppleImgbuf::init: suspicious image size request %d x %d\n",
          width, height);
      init (type, 1, 1, gamma, root);
   } else if (width > MAX_WIDTH) {
      MSG("AppleImgbuf::init: cannot handle large width %d\n", width);
      init (type, MAX_WIDTH, height, gamma, root);
   } else if (height > MAX_HEIGHT) {
      MSG("AppleImgbuf::init: cannot handle large height %d\n", height);
      init (type, width, MAX_HEIGHT, gamma, root);
   } else if (gamma <= 0) {
      MSG("AppleImgbuf::init: non-positive gamma %g\n", gamma);
      init (type, width, height, 1, root);
   } else {
      this->root = root;
      this->type = type;
      this->width = width;
      this->height = height;
      this->gamma = gamma;

      switch (type) {
      case RGBA: bpp = 4; break;
      case RGB:  bpp = 3; break;
      default:   bpp = 1; break;
      }
      rawdata = new unsigned char[bpp * width * height];
      // Light-gray interim background, as on the desktop.
      memset(rawdata, 222, width*height*bpp);

      refCount = 1;
      deleteOnUnref = true;
      copiedRows = new lout::misc::BitSet (height);

      // The list is only used for root buffers.
      if (isRoot())
         scaledBuffers = new lout::container::typed::List <AppleImgbuf>(true);
      else
         scaledBuffers = NULL;

      if (!isRoot()) {
         // Scaling
         for (int row = 0; row < root->height; row++) {
            if (root->copiedRows->get (row))
               scaleRow (row, root->rawdata + row*root->width*root->bpp);
         }
      }
   }
}

AppleImgbuf::~AppleImgbuf ()
{
   if (!isRoot())
      root->detachScaledBuf (this);

   delete[] rawdata;
   delete copiedRows;

   if (scaledBuffers)
      delete scaledBuffers;
}

/**
 * \brief Called for the root buffer when a scaled buffer is removed.
 */
void AppleImgbuf::detachScaledBuf (AppleImgbuf *scaledBuf)
{
   scaledBuffers->detachRef (scaledBuf);

   if (refCount == 0 && scaledBuffers->isEmpty () && deleteOnUnref)
      delete this;
}

void AppleImgbuf::setCMap (int *colors, int num_colors)
{
}

void AppleImgbuf::scaleRow (int row, const core::byte *data)
{
   if (row < root->height) {
      if (scaleMode == SIMPLE)
         scaleRowSimple (row, data);
      else
         scaleRowBeautiful (row, data);
   }
}

void AppleImgbuf::scaleRowSimple (int row, const core::byte *data)
{
   int sr1 = scaledY (row);
   int sr2 = scaledY (row + 1);

   for (int sr = sr1; sr < sr2; sr++) {
      if (copiedRows->get(sr)) continue;
      copiedRows->set (sr, true);

      if (sr == sr1) {
         for (int px = 0; px < root->width; px++) {
            int px1 = px * width / root->width;
            int px2 = (px+1) * width / root->width;
            for (int sp = px1; sp < px2; sp++) {
               memcpy(rawdata + (sr*width + sp)*bpp, data + px*bpp, bpp);
            }
         }
      } else {
         memcpy(rawdata + sr*width*bpp, rawdata + sr1*width*bpp, width*bpp);
      }
   }
}

void AppleImgbuf::scaleRowBeautiful (int row, const core::byte *data)
{
   int sr1 = scaledY (row);
   int sr2 = scaledY (row + 1);
   bool allRootRows = false;

   // Don't rescale rows!
   if (copiedRows->get(sr1)) return;

   if (height > root->height) {
      scaleBuffer (data, root->width, 1,
                   rawdata + sr1 * width * bpp, width, sr2 - sr1,
                   bpp, gamma);
      for (int sr = sr1; sr < sr2 || sr == sr1; sr++)
         copiedRows->set (sr, true);
   } else {
      assert (sr1 == sr2 || sr1 + 1 == sr2);
      int row1 = backscaledY(sr1), row2 = backscaledY(sr1 + 1);

      // Check all the necessary root lines already arrived,
      // a larger area than a single row may be accessed here.
      for (int r=row1; (allRootRows=root->copiedRows->get(r)) && ++r < row2; );
      if (allRootRows) {
         scaleBuffer (root->rawdata + row1 * root->width * bpp,
                      root->width, row2 - row1,
                      rawdata + sr1 * width * bpp, width, 1,
                      bpp, gamma);
         copiedRows->set (sr1, true);
      }
   }
}

/**
 * See FltkImgbuf::scaleBuffer: average of the source rectangle per
 * destination pixel, optionally in linear light (gamma-corrected).
 */
void AppleImgbuf::scaleBuffer (const core::byte *src, int srcWidth,
                               int srcHeight, core::byte *dest,
                               int destWidth, int destHeight, int bpp,
                               double gamma)
{
   unsigned char *gammaMap1 = NULL, *gammaMap2 = NULL;

   if (scaleMode == BEAUTIFUL_GAMMA) {
      gammaMap1 = findGammaCorrectionTable (gamma);
      gammaMap2 = findGammaCorrectionTable (1 / gamma);
   }

   int *v = new int[bpp];
   for(int x = 0; x < destWidth; x++) {
      for(int y = 0; y < destHeight; y++) {
         int xo1 = x * srcWidth / destWidth;
         int xo2 = lout::misc::max ((x + 1) * srcWidth / destWidth, xo1 + 1);
         int yo1 = y * srcHeight / destHeight;
         int yo2 = lout::misc::max ((y + 1) * srcHeight / destHeight, yo1 + 1);
         int n = (xo2 - xo1) * (yo2 - yo1);

         for(int i = 0; i < bpp; i++)
            v[i] = 0;

         for(int xo = xo1; xo < xo2; xo++)
            for(int yo = yo1; yo < yo2; yo++) {
               const core::byte *ps = src + bpp * (yo * srcWidth + xo);
               for(int i = 0; i < bpp; i++)
                  v[i] +=
                     (scaleMode == BEAUTIFUL_GAMMA ? gammaMap2[ps[i]] : ps[i]);
            }

         core::byte *pd = dest + bpp * (y * destWidth + x);
         for(int i = 0; i < bpp; i++)
            pd[i] =
               scaleMode == BEAUTIFUL_GAMMA ? gammaMap1[v[i] / n] : v[i] / n;
      }
   }
   delete[] v;
}

void AppleImgbuf::copyRow (int row, const core::byte *data)
{
   assert (isRoot());

   if (row < height) {
      copiedRows->set (row, true);
      memcpy(rawdata + row * width * bpp, data, width * bpp);

      for (Iterator <AppleImgbuf> it = scaledBuffers->iterator();
           it.hasNext(); ) {
         AppleImgbuf *sb = it.getNext ();
         sb->scaleRow (row, data);
      }
   }
}

void AppleImgbuf::newScan ()
{
   if (isRoot()) {
      for (Iterator<AppleImgbuf> it = scaledBuffers->iterator();
           it.hasNext();) {
         AppleImgbuf *sb = it.getNext ();
         sb->copiedRows->clear();
      }
   }
}

core::Imgbuf* AppleImgbuf::getScaledBuf (int width, int height)
{
   if (!isRoot())
      return root->getScaledBuf (width, height);

   if (width > MAX_WIDTH) {
      MSG("AppleImgbuf::getScaledBuf: cannot handle large width %d\n", width);
      return getScaledBuf (MAX_WIDTH, height);
   }
   if (height > MAX_HEIGHT) {
      MSG("AppleImgbuf::getScaledBuf: cannot handle large height %d\n",
          height);
      return getScaledBuf (width, MAX_HEIGHT);
   }

   if (width == this->width && height == this->height) {
      ref ();
      return this;
   }

   for (Iterator <AppleImgbuf> it = scaledBuffers->iterator();
        it.hasNext(); ) {
      AppleImgbuf *sb = it.getNext ();
      if (sb->width == width && sb->height == height) {
         sb->ref ();
         return sb;
      }
   }

   if (excessiveImageDimensions (width, height)) {
      MSG("AppleImgbuf::getScaledBuf: suspicious image size request "
          "%d x %d\n", width, height);
      ref ();
      return this;
   }

   AppleImgbuf *sb = new AppleImgbuf (type, width, height, gamma, this);
   scaledBuffers->append (sb);
   return sb;
}

void AppleImgbuf::getRowArea (int row, dw::core::Rectangle *area)
{
   if (isRoot()) {
      area->x = 0;
      area->y = row;
      area->width = width;
      area->height = 1;
   } else {
      if (row > root->height)
         area->x = area->y = area->width = area->height = 0;
      else {
         int sr1 = scaledY (row);
         int sr2 = scaledY (row + 1);

         area->x = 0;
         area->y = sr1;
         area->width = width;
         area->height = sr2 - sr1;
      }
   }
}

int AppleImgbuf::getRootWidth ()
{
   return root ? root->width : width;
}

int AppleImgbuf::getRootHeight ()
{
   return root ? root->height : height;
}

core::Imgbuf *AppleImgbuf::createSimilarBuf (int width, int height)
{
   return new AppleImgbuf (type, width, height, gamma);
}

void AppleImgbuf::copyTo (Imgbuf *dest, int xDestRoot, int yDestRoot,
                          int xSrc, int ySrc, int widthSrc, int heightSrc)
{
   AppleImgbuf *aDest = (AppleImgbuf*)dest;
   assert (bpp == aDest->bpp);

   int xSrc2 = lout::misc::min (xSrc + widthSrc, aDest->width - xDestRoot);
   int ySrc2 = lout::misc::min (ySrc + heightSrc, aDest->height - yDestRoot);

   for (int x = xSrc; x < xSrc2; x++)
      for (int y = ySrc; y < ySrc2; y++) {
         int iSrc = x + width * y;
         int iDest = xDestRoot + x + aDest->width * (yDestRoot + y);

         for (int b = 0; b < bpp; b++)
            aDest->rawdata[bpp * iDest + b] = rawdata[bpp * iSrc + b];
      }
}

void AppleImgbuf::ref ()
{
   refCount++;
}

void AppleImgbuf::unref ()
{
   if (--refCount == 0) {
      if (isRoot ()) {
         // Root buffer: only delete when no scaled buffers are left.
         if (scaledBuffers->isEmpty () && deleteOnUnref)
            delete this;
      } else
         delete this;
   }
}

bool AppleImgbuf::lastReference ()
{
   return refCount == 1 &&
      (scaledBuffers == NULL || scaledBuffers->isEmpty ());
}

void AppleImgbuf::setDeleteOnUnref (bool deleteOnUnref)
{
   assert (isRoot ());
   this->deleteOnUnref = deleteOnUnref;
}

bool AppleImgbuf::isReferred ()
{
   return refCount != 0 ||
      (scaledBuffers != NULL && !scaledBuffers->isEmpty ());
}

int AppleImgbuf::scaledY (int ySrc)
{
   assert (root != NULL);
   return ySrc * height / root->height;
}

int AppleImgbuf::backscaledY (int yScaled)
{
   assert (root != NULL);
   return yScaled * root->height / height;
}

void AppleImgbuf::draw (CGContextRef cg, int xRoot, int yRoot,
                        int x, int y, int width, int height)
{
   if (x > this->width || y > this->height)
      return;

   if (x + width > this->width)
      width = this->width - x;
   if (y + height > this->height)
      height = this->height - y;
   if (width <= 0 || height <= 0)
      return;

   /* Wrap the requested rows in a CGImage (no copy; the provider reads
    * straight from rawdata, which stays alive during the draw). */
   const unsigned char *base = rawdata + bpp * (y * this->width + x);
   size_t len = (size_t)bpp * this->width * height - bpp * x;
   CGDataProviderRef provider =
      CGDataProviderCreateWithData (NULL, base, len, NULL);
   CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB ();
   CGImageRef image =
      CGImageCreate (width, height, 8, 8 * bpp, bpp * this->width, space,
                     bpp == 4 ? (CGBitmapInfo)kCGImageAlphaLast
                              : (CGBitmapInfo)kCGImageAlphaNone,
                     provider, NULL, false, kCGRenderingIntentDefault);

   if (image) {
      /* CGContextDrawImage draws y-up; flip locally so the image comes out
       * upright in the y-down canvas context. */
      CGContextSaveGState (cg);
      CGContextTranslateCTM (cg, xRoot + x, yRoot + y + height);
      CGContextScaleCTM (cg, 1, -1);
      /* Row data is already scaled to this buffer's size: draw 1:1. */
      CGContextSetInterpolationQuality (cg, kCGInterpolationNone);
      CGContextDrawImage (cg, CGRectMake (0, 0, width, height), image);
      CGContextRestoreGState (cg);
      CGImageRelease (image);
   }

   CGColorSpaceRelease (space);
   CGDataProviderRelease (provider);
}

} // namespace apple
} // namespace dw
