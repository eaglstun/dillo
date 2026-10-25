/*
 * Dillo Widget — Apple (Core Graphics) image buffer.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __DW_APPLE_IMGBUF_HH__
#define __DW_APPLE_IMGBUF_HH__

#include <CoreGraphics/CoreGraphics.h>

#include "dw/core.hh"

namespace dw {
namespace apple {

/**
 * \brief dw::core::Imgbuf implementation, a port of dw::fltk::FltkImgbuf.
 *
 * The row/scaling machinery (root + scaled buffers, gamma-aware averaging)
 * is toolkit-independent and ported as-is; only draw() differs, wrapping
 * the raw RGB rows in a CGImage and drawing into the view's CGContext.
 */
class AppleImgbuf: public core::Imgbuf
{
private:
   class GammaCorrectionTable: public lout::object::Object
   {
   public:
      double gamma;
      unsigned char map[256];
   };

   AppleImgbuf *root;
   int refCount;
   bool deleteOnUnref;
   lout::container::typed::List <AppleImgbuf> *scaledBuffers;

   int width, height;
   Type type;
   double gamma;

   int bpp;
   unsigned char *rawdata;

   lout::misc::BitSet *copiedRows;

   static lout::container::typed::Vector <GammaCorrectionTable>
      *gammaCorrectionTables;

   static unsigned char *findGammaCorrectionTable (double gamma);
   static bool excessiveImageDimensions (int width, int height);

   AppleImgbuf (Type type, int width, int height, double gamma,
                AppleImgbuf *root);
   void init (Type type, int width, int height, double gamma,
              AppleImgbuf *root);
   int scaledY (int ySrc);
   int backscaledY (int yScaled);
   int isRoot () { return (root == NULL); }
   void detachScaledBuf (AppleImgbuf *scaledBuf);

   void scaleRow (int row, const core::byte *data);
   void scaleRowSimple (int row, const core::byte *data);
   void scaleRowBeautiful (int row, const core::byte *data);
   static void scaleBuffer (const core::byte *src, int srcWidth,
                            int srcHeight, core::byte *dest,
                            int destWidth, int destHeight, int bpp,
                            double gamma);

protected:
   ~AppleImgbuf ();

public:
   AppleImgbuf (Type type, int width, int height, double gamma);

   void setCMap (int *colors, int num_colors);
   void newScan ();
   void copyRow (int row, const core::byte *data);
   core::Imgbuf* getScaledBuf (int width, int height);
   void getRowArea (int row, dw::core::Rectangle *area);
   int getRootWidth ();
   int getRootHeight ();
   core::Imgbuf *createSimilarBuf (int width, int height);
   void copyTo (Imgbuf *dest, int xDestRoot, int yDestRoot,
                int xSrc, int ySrc, int widthSrc, int heightSrc);
   void ref ();
   void unref ();

   bool lastReference ();
   void setDeleteOnUnref (bool deleteOnUnref);
   bool isReferred ();

   /**
    * \brief Draw the region (x, y, width, height) of this buffer with its
    *    top-left corner at canvas position (xRoot + x, yRoot + y), into a
    *    UIKit-oriented (y-down) context.
    */
   void draw (CGContextRef cg, int xRoot, int yRoot,
              int x, int y, int width, int height);
};

} // namespace apple
} // namespace dw

#endif // __DW_APPLE_IMGBUF_HH__
