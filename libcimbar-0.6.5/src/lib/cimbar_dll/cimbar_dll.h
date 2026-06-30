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

/* Initialize encoding for a file.
 * filename: path to the file to encode
 * mode_val: encoding mode (4=4C, 66=Bu, 67=Bm, 68=B)
 * compression: zstd compression level (1-22, 0=disable, default=16)
 * Returns: 0 on success, negative on error
 */
CIMBAR_API int cimbar_encode_file(const char* filename, int mode_val, int compression);

/* Initialize encoding from a memory buffer.
 * data: pointer to the data to encode
 * size: size of the data in bytes
 * filename: virtual filename for metadata header (can be NULL)
 * mode_val: encoding mode (4=4C, 66=Bu, 67=Bm, 68=B)
 * compression: zstd compression level (1-22, 0=disable, default=16)
 * Returns: 0 on success, negative on error
 */
CIMBAR_API int cimbar_encode_buffer(const unsigned char* data, int size,
                                    const char* filename, int mode_val, int compression);

/* Initialize encoding from a memory buffer with explicit encode_id.
 * 大文件分块传输：每个 chunk 使用不同 encode_id，解码端自动分桶。
 * data: pointer to the data to encode
 * size: size of the data in bytes
 * filename: virtual filename for metadata header (can be NULL)
 * mode_val: encoding mode (4=4C, 66=Bu, 67=Bm, 68=B)
 * compression: zstd compression level (1-22, 0=disable, default=16)
 * encode_id: fountain stream ID (0-127). -1 = use internal default.
 * Returns: 0 on success, negative on error
 */
CIMBAR_API int cimbar_encode_buffer_ex(const unsigned char* data, int size,
                                       const char* filename, int mode_val, int compression,
                                       int encode_id);

/* Generate the next encoded frame.
 * Returns: frame number (>0) on success, 0 if no more frames, negative on error
 */
CIMBAR_API int cimbar_encode_next_frame();

/* Get the width of the current encoded frame.
 * Returns: width in pixels, or negative if no frame available
 */
CIMBAR_API int cimbar_encode_get_frame_width();

/* Get the height of the current encoded frame.
 * Returns: height in pixels, or negative if no frame available
 */
CIMBAR_API int cimbar_encode_get_frame_height();

/* Get the size in bytes of the current encoded frame data (width * height * 3 for RGB).
 * Returns: size in bytes, or negative if no frame available
 */
CIMBAR_API int cimbar_encode_get_frame_size();

/* Copy the current frame data (RGB format) to the provided buffer.
 * buffer: destination buffer
 * bufsize: size of the destination buffer
 * Returns: bytes copied, or negative on error
 */
CIMBAR_API int cimbar_encode_copy_frame(unsigned char* buffer, int bufsize);

/* Copy the current frame data in BGRA format (GDI+ compatible) to the provided buffer.
 * Each pixel is 4 bytes: Blue, Green, Red, Alpha(0xFF).
 * buffer: destination buffer (size >= width * height * 4)
 * bufsize: size of the destination buffer
 * Returns: bytes copied, or negative on error
 */
CIMBAR_API int cimbar_encode_copy_frame_bgra(unsigned char* buffer, int bufsize);

/* Get encoding info: recommended frames, current frame, blocks required, blocks generated.
 * recommended: output - recommended number of frames for one cycle (4x redundancy)
 * current: output - current frame number
 * blocks_required: output - minimum blocks needed for decoding
 * blocks_generated: output - blocks generated so far
 * Returns: 0 on success, negative on error
 */
CIMBAR_API int cimbar_encode_get_info(int* recommended, int* current, int* blocks_required, int* blocks_generated);

/* Create an HBITMAP from the current encoded frame (32-bit BGRA DIB Section).
 * The caller is responsible for calling DeleteObject() on the returned HBITMAP
 * when it is no longer needed. GdipCreateBitmapFromHBITMAP will copy the data,
 * so the HBITMAP can be deleted immediately after creating a gdip.bitmap.
 * Returns: HBITMAP handle, or NULL on error
 */
CIMBAR_API void* cimbar_encode_create_hbitmap();

/* Cleanup encoding state and release resources. */
CIMBAR_API void cimbar_encode_cleanup();

/* Save the current encoder stream to cache (indexed by encode_id).
 * Call before switching to another chunk in big file mode.
 * encode_id: fountain stream ID (0-127)
 * Returns: 0 on success, negative on error
 */
CIMBAR_API int cimbar_encode_save_stream(int encode_id);

/* Restore an encoder stream from cache (indexed by encode_id).
 * Call when switching back to a previously saved chunk.
 * encode_id: fountain stream ID (0-127)
 * Returns: 0 on success (stream restored), 1 if not in cache, negative on error
 */
CIMBAR_API int cimbar_encode_restore_stream(int encode_id);

/* ===== Decoding API ===== */

/* Configure the decode mode.
 * mode_val: encoding mode (4=4C, 66=Bu, 67=Bm, 68=B)
 * Returns: 0 on success, negative on error
 */
CIMBAR_API int cimbar_decode_configure(int mode_val);

/* Get the required buffer size for decode output.
 * Returns: buffer size in bytes
 */
CIMBAR_API int cimbar_decode_get_bufsize();

/* Scan, extract, and decode a camera frame.
 * imgdata: raw image data (RGB=3, RGBA=4, YUV_NV12=12, YUV420p=420)
 * imgw: image width
 * imgh: image height
 * format: pixel format (3=RGB, 4=RGBA, 12=NV12, 420=YUV420p)
 * bufspace: output buffer for decoded data
 * bufsize: size of the output buffer
 * Returns: bytes decoded, or negative on error
 */
CIMBAR_API int cimbar_decode_scan_extract(const unsigned char* imgdata, int imgw, int imgh, int format,
                                           unsigned char* bufspace, int bufsize);

/* Feed decoded data to the fountain decoder.
 * buffer: data from scan_extract
 * size: size of the data
 * Returns: file id (>0) if decoding is complete, 0 if more data needed, negative on error
 */
CIMBAR_API int64_t cimbar_decode_fountain(const unsigned char* buffer, int size);

/* Get the compressed file size for a decoded file.
 * id: file id returned by fountain_decode
 * Returns: file size in bytes
 */
CIMBAR_API unsigned cimbar_decode_get_filesize(uint32_t id);

/* Get the original filename of a decoded file.
 * id: file id returned by fountain_decode
 * filename: output buffer for the filename
 * fnsize: size of the output buffer
 * Returns: length of filename, or negative on error
 */
CIMBAR_API int cimbar_decode_get_filename(uint32_t id, char* filename, int fnsize);

/* Get the required buffer size for decompression output.
 * Returns: buffer size in bytes
 */
CIMBAR_API int cimbar_decode_get_decompress_bufsize();

/* Decompress and read the decoded file contents.
 * id: file id returned by fountain_decode
 * buffer: output buffer for decompressed data
 * size: size of the output buffer
 * Returns: bytes read, or negative on error
 */
CIMBAR_API int cimbar_decode_decompress_read(uint32_t id, unsigned char* buffer, int size);

/* Cleanup decode state and release resources. */
CIMBAR_API void cimbar_decode_cleanup();

/* One-step decode: read image file, scan, extract, and fountain decode.
 * img_path: path to the image file (UTF-8)
 * mode_val: encoding mode (4=4C, 66=Bu, 67=Bm, 68=B, 0=auto-detect)
 * Returns: file id (>0) if decoding is complete, 0 if more data needed,
 *          -1=extract failed, -2=read failed, -3=config error
 */
CIMBAR_API int64_t cimbar_decode_image(const char* img_path, int mode_val);

/* Get the number of active decode streams. */
CIMBAR_API int cimbar_decode_get_num_streams();

/* Get the progress of a specific decode stream (0-100). */
CIMBAR_API int cimbar_decode_get_stream_progress(int stream_idx);

/* Get the number of completed decodes. */
CIMBAR_API int cimbar_decode_get_num_done();

/* Get the scan result from the last decode call.
 * Returns: 0=FAILURE (anchors not detected),
 *          1=SUCCESS (anchors detected, extract OK),
 *          2=NEEDS_SHARPEN (anchors detected but image blurry)
 */
CIMBAR_API int cimbar_decode_get_scan_result();

/* ===== Camera API ===== */

/* Open a camera device using Media Foundation.
 * index: camera device index (0 = default camera)
 * Returns: 0 on success, negative on error
 */
CIMBAR_API int cimbar_cam_open(int index);

/* Grab a frame from the camera and return as HBITMAP (32-bit BGRA DIB Section).
 * The caller is responsible for calling DeleteObject() on the returned HBITMAP.
 * Returns: HBITMAP handle, or NULL on error
 */
CIMBAR_API void* cimbar_cam_grab_hbitmap();

/* Close the camera and release resources. */
CIMBAR_API void cimbar_cam_close();

/* Get the width of the camera frame.
 * Returns: width in pixels, or 0 if camera not open
 */
CIMBAR_API int cimbar_cam_get_width();

/* Get the height of the camera frame.
 * Returns: height in pixels, or 0 if camera not open
 */
CIMBAR_API int cimbar_cam_get_height();

#ifdef __cplusplus
}
#endif

#endif // CIMBAR_DLL_API_H
