#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <tiffio.h>
#include <png.h>
#include <endian.h>

static int writeRGBAPNG(const uint32_t* raster, const int w, const int h, const char* filename);
int convert_image_tiff_png(const char* in, const char* out) {
    TIFF *tif = TIFFOpen(in, "r");
    if (tif == NULL) {
        perror("TIFFOpen()");
        return errno;
    }

    uint32_t w, h;
    if (TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w) != 1|| TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h) != 1) {
        perror("TIFFGetField()");
        return errno;
    }

    if (w < 1 || h < 1) {
        fprintf(stderr, "Image is invalid size!\n");
        return -1;
    }

    uint32_t *raster;
    if ((raster = _TIFFmalloc(w * h * sizeof(uint32_t))) == NULL) {
        perror("_TIFFMalloc()");
        return errno;
    }

    if (TIFFReadRGBAImage(tif, w, h, raster, 0) == 0) {
        perror("TIFFReadRGBAImage()");
        return errno;
    }

    if (writeRGBAPNG(raster, w, h, out) != 0) {
        perror("writeRGBAPNG()");
        return errno;
    }

    _TIFFfree(raster);
    TIFFClose(tif);

    return 0;
}

static int writeRGBAPNG(const uint32_t* raster, const int w, const int h, const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (fp == NULL) {
        perror("fopen()");
        return errno;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        perror("png_create_write_struct()");
        return errno;
    }
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        perror("png_create_info_struct()");
        return errno;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "PNG other error\n");
        return errno;
    }

    png_set_IHDR(png_ptr, info_ptr, w, h, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_byte **row_pointers = png_malloc(png_ptr, h * sizeof(png_byte*));
    for (int y=0;y<h;y++) {
        png_byte *row = row_pointers[h-(y+1)] = png_malloc(png_ptr, sizeof(uint8_t) * w * 4);
        
        for (int x=0;x<w;x++) {
            uint32_t raster_pix = htole32(raster[x + (y*w)]);
            *row++ = raster_pix & 0xFF;
            *row++ = (raster_pix & 0xFF00) >> 8;
            *row++ = (raster_pix & 0xFF0000) >> 16;
            *row++ = (raster_pix & 0xFF000000) >> 24;
        }
    }

    png_init_io (png_ptr, fp);
    png_set_rows (png_ptr, info_ptr, row_pointers);
    png_write_png (png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

    for (int y=0;y<h;y++) {
        png_free (png_ptr, row_pointers[y]);
    }
    png_free (png_ptr, row_pointers);

    fclose(fp);
}