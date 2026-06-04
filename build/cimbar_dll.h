/* This code is subject to the terms of the Mozilla Public License, v.2.0. http://mozilla.org/MPL/2.0/. */
#ifndef CIMBAR_DLL_API_H
#define CIMBAR_DLL_API_H

#include <stdint.h>

#ifdef CIMBAR_DLL_EXPORTS
#define CIMBAR_API __declspec(dllexport)
#else
#define CIMBAR_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Encoding API ===== */

CIMBAR_API int cimbar_encode_file(const char* filename, int mode_val, int compression);
CIMBAR_API int cimbar_encode_next_frame();
CIMBAR_API int cimbar_encode_get_frame_width();
CIMBAR_API int cimbar_encode_get_frame_height();
CIMBAR_API int cimbar_encode_get_frame_size();
CIMBAR_API int cimbar_encode_copy_frame(unsigned char* buffer, int bufsize);
CIMBAR_API int cimbar_encode_copy_frame_bgra(unsigned char* buffer, int bufsize);
CIMBAR_API int cimbar_encode_get_info(int* recommended, int* current, int* blocks_required, int* blocks_generated);

/* Create an HBITMAP from the current encoded frame (32-bit BGRA DIB Section).
 * The caller is responsible for calling DeleteObject() on the returned HBITMAP.
 * GdipCreateBitmapFromHBITMAP copies the data, so the HBITMAP can be deleted
 * immediately after creating a gdip.bitmap.
 * Returns: HBITMAP handle, or NULL on error
 */
CIMBAR_API void* cimbar_encode_create_hbitmap();

CIMBAR_API void cimbar_encode_cleanup();

/* ===== Decoding API ===== */

CIMBAR_API int cimbar_decode_configure(int mode_val);
CIMBAR_API int cimbar_decode_get_bufsize();
CIMBAR_API int cimbar_decode_scan_extract(const unsigned char* imgdata, int imgw, int imgh, int format,
                                           unsigned char* bufspace, int bufsize);
CIMBAR_API int64_t cimbar_decode_fountain(const unsigned char* buffer, int size);
CIMBAR_API unsigned cimbar_decode_get_filesize(uint32_t id);
CIMBAR_API int cimbar_decode_get_filename(uint32_t id, char* filename, int fnsize);
CIMBAR_API int cimbar_decode_get_decompress_bufsize();
CIMBAR_API int cimbar_decode_decompress_read(uint32_t id, unsigned char* buffer, int size);
CIMBAR_API void cimbar_decode_cleanup();
CIMBAR_API int64_t cimbar_decode_image(const char* img_path, int mode_val);
CIMBAR_API int cimbar_decode_get_num_streams();
CIMBAR_API int cimbar_decode_get_stream_progress(int stream_idx);
CIMBAR_API int cimbar_decode_get_num_done();

#ifdef __cplusplus
}
#endif

#endif // CIMBAR_DLL_API_H
