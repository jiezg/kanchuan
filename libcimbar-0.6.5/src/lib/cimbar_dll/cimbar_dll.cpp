/* This code is subject to the terms of the Mozilla Public License, v.2.0. http://mozilla.org/MPL/2.0/. */
#include "cimbar_dll.h"

#include "cimb_translator/Config.h"
#include "compression/zstd_compressor.h"
#include "compression/zstd_header_check.h"
#include "encoder/Encoder.h"
#include "encoder/Decoder.h"
#include "encoder/escrow_buffer_writer.h"
#include "extractor/Extractor.h"
#include "fountain/FountainInit.h"
#include "fountain/fountain_decoder_sink.h"
#include "serialize/str_join.h"
#include "util/File.h"
#include "util/Timer.h"

#include <opencv2/opencv.hpp>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

/* ===== Unicode Path Helper ===== */
// Windows std::ifstream doesn't support UTF-8 paths.
// Convert UTF-8 to wide string and use std::filesystem::path.
static std::wstring utf8_to_wide(const std::string& str) {
	if (str.empty()) return std::wstring();
	int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	if (size <= 0) return std::wstring();
	std::wstring result(size - 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
	return result;
}

static std::ifstream open_file_unicode(const char* filename, std::ios_base::openmode mode) {
	std::wstring wpath = utf8_to_wide(filename);
	std::filesystem::path fspath(wpath);
	return std::ifstream(fspath, mode);
}

/* ===== Encoding State ===== */
namespace encode {
	std::shared_ptr<fountain_encoder_stream> fes;
	std::optional<cv::Mat> nextFrame;
	uint8_t encodeId = 109;
	int frameCount = 0;
	int modeVal = 68;
	int compressionLevel = cimbar::Config::compression_level();
}

/* ===== Decoding State ===== */
namespace decode {
	std::shared_ptr<fountain_decoder_sink> sink;
	uint32_t decId = 0;
	std::vector<uchar> reassembled;
	std::unique_ptr<cimbar::zstd_decompressor<std::stringstream>> dec;
	int modeVal = 68;
	int lastScanResult = 0;

	int init_decompress(uint32_t id)
	{
		if (id != decId)
			return -11;
		if (dec)
			dec.reset();
		dec = std::make_unique<cimbar::zstd_decompressor<std::stringstream>>();
		if (!dec)
			return -12;
		dec->init_decompress(reinterpret_cast<char*>(reassembled.data()), reassembled.size());
		return 0;
	}

	int recover_contents(uint32_t id)
	{
		if (id != decId)
		{
			if (!sink)
				return -1;
			if (sink->is_done(id))
				return -2;

			reassembled.resize(cimbar_decode_get_filesize(id));
			if (!sink->recover(id, reassembled.data(), reassembled.size()))
				return -3;
			decId = id;

			int res = init_decompress(id);
			if (res < 0)
				return res;
		}
		if (reassembled.empty())
			return -5;
		return 0;
	}

	unsigned fountain_chunks_per_frame()
	{
		return cimbar::Config::fountain_chunks_per_frame(cimbar::Config::bits_per_cell());
	}

	unsigned fountain_chunk_size()
	{
		return cimbar::Config::fountain_chunk_size();
	}

	cv::UMat get_rgb(void* imgdata, int width, int height, int type)
	{
		cv::UMat img;
		switch (type)
		{
			case 12:
			{
				img = cv::Mat(height * 3/2, width, CV_8UC1, imgdata).getUMat(cv::ACCESS_RW).clone();
				cv::cvtColor(img, img, cv::COLOR_YUV2RGB_NV12);
				return img;
			}
			case 420:
			{
				img = cv::Mat(height * 3/2, width, CV_8UC1, imgdata).getUMat(cv::ACCESS_RW).clone();
				cv::cvtColor(img, img, cv::COLOR_YUV420p2RGB);
				return img;
			}
			default:
				break;
		}

		int cvtype = type == 4 ? CV_8UC4 : CV_8UC3;
		img = cv::Mat(height, width, cvtype, imgdata).getUMat(cv::ACCESS_RW).clone();
		if (type == 4)
			cv::cvtColor(img, img, cv::COLOR_RGBA2RGB);
		return img;
	}
}

/* ===== Encoding API Implementation ===== */

extern "C" {

int cimbar_encode_file(const char* filename, int mode_val, int compression)
{
	if (!filename || filename[0] == '\0')
		return -1;

	// Configure mode
	cimbar::Config::update(mode_val);
	encode::modeVal = mode_val;

	if (compression < 0 || compression > 22)
		compression = cimbar::Config::compression_level();
	encode::compressionLevel = compression;

	// Initialize fountain codec
	if (!FountainInit::init())
		return -5;

	// Open and compress the file (Unicode path support)
	std::ifstream ifs = open_file_unicode(filename, std::ios::binary);
	if (!ifs.is_open())
		return -2;

	auto comp = std::make_unique<cimbar::zstd_compressor<std::stringstream>>();
	if (!comp)
		return -3;

	comp->set_compression_level(compression);

	// Write filename header
	std::string fn = File::basename(filename);
	if (!fn.empty())
		comp->write_header(fn.data(), fn.size());

	// Compress the file data
	if (!comp->compress(ifs))
		return -4;

	// Pad if necessary
	unsigned fountainChunkSize = cimbar::Config::fountain_chunk_size();
	size_t compressedSize = comp->size();
	if (compressedSize < fountainChunkSize)
		comp->pad(fountainChunkSize - compressedSize + 1);

	// Create fountain encoder stream
	encode::fes = fountain_encoder_stream::create(*comp, fountainChunkSize, encode::encodeId);
	if (!encode::fes)
		return -6;

	encode::nextFrame.reset();
	encode::frameCount = 0;
	return 0;
}

int cimbar_encode_next_frame()
{
	if (!encode::fes)
		return -1;

	// Check if we should restart (generate enough blocks for decoder)
	unsigned required = encode::fes->blocks_required() * 8;
	if (encode::fes->block_count() > required)
	{
		encode::fes->restart();
		encode::frameCount = 0;
	}

	Encoder enc;
	enc.set_encode_id(encode::encodeId);
	encode::nextFrame = enc.encode_next(*encode::fes);
	return ++encode::frameCount;
}

int cimbar_encode_get_frame_width()
{
	if (!encode::nextFrame || encode::nextFrame->cols == 0)
		return -1;
	return encode::nextFrame->cols;
}

int cimbar_encode_get_frame_height()
{
	if (!encode::nextFrame || encode::nextFrame->rows == 0)
		return -1;
	return encode::nextFrame->rows;
}

int cimbar_encode_get_frame_size()
{
	if (!encode::nextFrame || encode::nextFrame->cols == 0 || encode::nextFrame->rows == 0)
		return -1;
	return encode::nextFrame->cols * encode::nextFrame->rows * encode::nextFrame->channels();
}

int cimbar_encode_copy_frame(unsigned char* buffer, int bufsize)
{
	if (!encode::nextFrame || encode::nextFrame->cols == 0 || encode::nextFrame->rows == 0)
		return -1;

	int dataSize = encode::nextFrame->cols * encode::nextFrame->rows * encode::nextFrame->channels();
	if (bufsize < dataSize)
		return -2;

	// Ensure the Mat is continuous in memory
	if (encode::nextFrame->isContinuous())
	{
		std::copy(encode::nextFrame->data, encode::nextFrame->data + dataSize, buffer);
	}
	else
	{
		int rows = encode::nextFrame->rows;
		int cols = encode::nextFrame->cols;
		int channels = encode::nextFrame->channels();
		int rowSize = cols * channels;
		for (int r = 0; r < rows; ++r)
		{
			std::copy(encode::nextFrame->ptr<uchar>(r),
			          encode::nextFrame->ptr<uchar>(r) + rowSize,
			          buffer + r * rowSize);
		}
	}

	return dataSize;
}

int cimbar_encode_copy_frame_bgra(unsigned char* buffer, int bufsize)
{
	if (!encode::nextFrame || encode::nextFrame->cols == 0 || encode::nextFrame->rows == 0)
		return -1;

	int w = encode::nextFrame->cols;
	int h = encode::nextFrame->rows;
	int bgraSize = w * h * 4;
	if (bufsize < bgraSize)
		return -2;

	// 将 RGB 转换为 BGRA（GDI+ PixelFormat32bppARGB 内存布局）
	const cv::Mat& frame = *encode::nextFrame;
	int channels = frame.channels();
	for (int r = 0; r < h; ++r)
	{
		const uchar* srcRow = frame.ptr<uchar>(r);
		unsigned char* dstRow = buffer + r * w * 4;
		if (channels == 3)
		{
			for (int c = 0; c < w; ++c)
			{
				dstRow[c * 4 + 0] = srcRow[c * 3 + 2]; // B
				dstRow[c * 4 + 1] = srcRow[c * 3 + 1]; // G
				dstRow[c * 4 + 2] = srcRow[c * 3 + 0]; // R
				dstRow[c * 4 + 3] = 0xFF;               // A
			}
		}
		else if (channels == 4)
		{
			for (int c = 0; c < w; ++c)
			{
				dstRow[c * 4 + 0] = srcRow[c * 4 + 2]; // B
				dstRow[c * 4 + 1] = srcRow[c * 4 + 1]; // G
				dstRow[c * 4 + 2] = srcRow[c * 4 + 0]; // R
				dstRow[c * 4 + 3] = 0xFF;               // A
			}
		}
	}

	return bgraSize;
}

void* cimbar_encode_create_hbitmap()
{
	if (!encode::nextFrame || encode::nextFrame->cols == 0 || encode::nextFrame->rows == 0)
		return NULL;

	int w = encode::nextFrame->cols;
	int h = encode::nextFrame->rows;
	const cv::Mat& frame = *encode::nextFrame;
	int channels = frame.channels();

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = w;
	bmi.bmiHeader.biHeight = -h;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	HDC hdc = GetDC(NULL);
	void* bits = NULL;
	HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	ReleaseDC(NULL, hdc);

	if (!hBitmap || !bits)
		return NULL;

	unsigned char* dst = (unsigned char*)bits;
	for (int r = 0; r < h; ++r)
	{
		const uchar* srcRow = frame.ptr<uchar>(r);
		unsigned char* dstRow = dst + r * w * 4;
		if (channels == 3)
		{
			for (int c = 0; c < w; ++c)
			{
				dstRow[c*4+0] = srcRow[c*3+2];
				dstRow[c*4+1] = srcRow[c*3+1];
				dstRow[c*4+2] = srcRow[c*3+0];
				dstRow[c*4+3] = 0xFF;
			}
		}
		else if (channels == 4)
		{
			for (int c = 0; c < w; ++c)
			{
				dstRow[c*4+0] = srcRow[c*4+2];
				dstRow[c*4+1] = srcRow[c*4+1];
				dstRow[c*4+2] = srcRow[c*4+0];
				dstRow[c*4+3] = 0xFF;
			}
		}
	}

	return (void*)hBitmap;
}

void cimbar_encode_cleanup()
{
	encode::fes.reset();
	encode::nextFrame.reset();
	encode::frameCount = 0;
}

int cimbar_encode_get_info(int* recommended, int* current, int* blocks_required, int* blocks_generated)
{
	if (!encode::fes)
		return -1;

	if (recommended) {
		unsigned chunksPerFrame = cimbar::Config::fountain_chunks_per_frame(cimbar::Config::bits_per_cell());
		*recommended = (encode::fes->blocks_required() * 4 + chunksPerFrame - 1) / chunksPerFrame;
	}
	if (current)
		*current = encode::frameCount;
	if (blocks_required)
		*blocks_required = encode::fes->blocks_required();
	if (blocks_generated)
		*blocks_generated = encode::fes->block_count();

	return 0;
}

/* ===== Decoding API Implementation ===== */

int cimbar_decode_configure(int mode_val)
{
	if (mode_val <= 0)
		mode_val = 68;

	bool refresh = (mode_val != decode::modeVal);
	if (refresh)
	{
		decode::modeVal = mode_val;
		cimbar::Config::update(mode_val);
		decode::sink.reset();
	}

	return 0;
}

int cimbar_decode_get_bufsize()
{
	return decode::fountain_chunks_per_frame() * decode::fountain_chunk_size();
}

int cimbar_decode_scan_extract(const unsigned char* imgdata, int imgw, int imgh, int format,
                               unsigned char* bufspace, int bufsize)
{
	if (format <= 0)
		format = 3;
	if (imgw == 0 || imgh == 0)
		return -1;

	unsigned chunksPerFrame = decode::fountain_chunks_per_frame();
	unsigned chunkSize = decode::fountain_chunk_size();
	if (bufsize < (int)(chunkSize * chunksPerFrame))
		return -2;

	escrow_buffer_writer ebw(bufspace, chunksPerFrame, chunkSize);
	Extractor ext;
	Decoder dec;

	cv::UMat img = decode::get_rgb((void*)imgdata, imgw, imgh, format);

	bool shouldPreprocess = true;
	{
		int res = ext.extract(img, img);
		decode::lastScanResult = res; // 保存扫描结果供 cimbar_decode_get_scan_result 查询
		if (!res)
			return -3;
		else if (res == Extractor::NEEDS_SHARPEN)
			shouldPreprocess = true;
	}

	dec.decode_fountain(img, ebw, shouldPreprocess);
	return ebw.buffers_in_use() * chunkSize;
}

int64_t cimbar_decode_fountain(const unsigned char* buffer, int size)
{
	unsigned chunkSize = decode::fountain_chunk_size();
	if (!decode::sink)
		decode::sink = std::make_shared<fountain_decoder_sink>(chunkSize);

	if (size == 0 || size % (int)chunkSize != 0)
		return -5;

	int64_t res = 0;
	for (int i = 0; i < size && res == 0; i += chunkSize)
	{
		res = decode::sink->decode_frame(reinterpret_cast<const char*>(buffer + i), chunkSize);
	}

	return res;
}

unsigned cimbar_decode_get_filesize(uint32_t id)
{
	FountainMetadata md(id);
	return md.file_size();
}

int cimbar_decode_get_filename(uint32_t id, char* filename, int fnsize)
{
	int res = decode::recover_contents(id);
	if (res < 0)
		return res;

	const uchar* finbuffer = decode::reassembled.data();
	unsigned size = decode::reassembled.size();

	std::string fn = cimbar::zstd_header_check::get_filename(finbuffer, size);
	if (!fn.empty())
		fn = File::basename(fn);
	if (fn.empty())
		return 0;

	if (fnsize < (int)fn.size())
		fn.resize(fnsize);
	std::copy(fn.begin(), fn.end(), filename);
	return fn.size();
}

int cimbar_decode_get_decompress_bufsize()
{
	return ZSTD_DStreamOutSize();
}

int cimbar_decode_decompress_read(uint32_t id, unsigned char* buffer, int size)
{
	int res = decode::recover_contents(id);
	if (res < 0)
		return res;

	if (!decode::dec)
		return -13;
	if (!decode::dec->good())
		return -14;

	decode::dec->str(std::string());
	decode::dec->write_once();
	std::string temp = decode::dec->str();
	if (size > (int)temp.size())
		size = temp.size();
	std::copy(temp.data(), temp.data() + size, buffer);
	return size;
}

void cimbar_decode_cleanup()
{
	decode::sink.reset();
	decode::dec.reset();
	decode::reassembled.clear();
	decode::decId = 0;
}

int64_t cimbar_decode_image(const char* img_path, int mode_val)
{
	if (!img_path || img_path[0] == '\0')
		return -2;

	// Configure mode
	if (mode_val > 0)
		cimbar_decode_configure(mode_val);

	// Read image with Unicode path support
	std::wstring wpath = utf8_to_wide(img_path);
	FILE* fp = _wfopen(wpath.c_str(), L"rb");
	if (!fp) return -2;

	fseek(fp, 0, SEEK_END);
	long fileSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (fileSize <= 0) { fclose(fp); return -2; }

	std::vector<uchar> fileBuf(fileSize);
	fread(fileBuf.data(), 1, fileSize, fp);
	fclose(fp);

	cv::Mat img = cv::imdecode(fileBuf, cv::IMREAD_COLOR);
	if (img.empty()) return -2;

	// Convert BGR to RGB
	cv::Mat rgb;
	cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);

	// Scan and extract using existing API
	int bufsize = cimbar_decode_get_bufsize();
	std::vector<unsigned char> bufspace(bufsize);
	int bytesDecoded = cimbar_decode_scan_extract(rgb.data, rgb.cols, rgb.rows, 3, bufspace.data(), bufsize);
	if (bytesDecoded < 0)
		return -1; // extract failed

	// Fountain decode using existing API
	return cimbar_decode_fountain(bufspace.data(), bytesDecoded);
}

int cimbar_decode_get_num_streams()
{
	if (!decode::sink) return 0;
	return decode::sink->num_streams();
}

int cimbar_decode_get_stream_progress(int stream_idx)
{
	if (!decode::sink) return 0;
	auto progress = decode::sink->get_progress();
	if (stream_idx < 0 || stream_idx >= (int)progress.size())
		return 0;
	return (int)(progress[stream_idx] * 100);
}

int cimbar_decode_get_num_done()
{
	if (!decode::sink) return 0;
	return decode::sink->num_done();
}

int cimbar_decode_get_scan_result()
{
	return decode::lastScanResult;
}

/* ===== Camera API Implementation ===== */

namespace camera {
	IMFSourceReader* pReader = nullptr;
	IMFMediaSource* pSource = nullptr;
	int camWidth = 0;
	int camHeight = 0;
	bool isOpen = false;

	// 释放所有摄像头资源并关闭 Media Foundation
	void cleanup()
	{
		if (pReader) { pReader->Release(); pReader = nullptr; }
		if (pSource) { pSource->Shutdown(); pSource->Release(); pSource = nullptr; }
		camWidth = 0;
		camHeight = 0;
		isOpen = false;
		MFShutdown();
	}
}

int cimbar_cam_open(int index)
{
	if (camera::isOpen) cimbar_cam_close();

	// 初始化 Media Foundation
	HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
	if (FAILED(hr)) return -1;

	// 枚举视频捕获设备
	IMFAttributes* pDevAttr = nullptr;
	hr = MFCreateAttributes(&pDevAttr, 1);
	if (FAILED(hr)) { MFShutdown(); return -2; }

	hr = pDevAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
	                        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
	if (FAILED(hr)) { pDevAttr->Release(); MFShutdown(); return -3; }

	IMFActivate** ppDevices = nullptr;
	UINT32 count = 0;
	hr = MFEnumDeviceSources(pDevAttr, &ppDevices, &count);
	pDevAttr->Release();

	if (FAILED(hr) || count == 0 || (UINT32)index >= count) {
		if (ppDevices) {
			for (UINT32 i = 0; i < count; i++) ppDevices[i]->Release();
			CoTaskMemFree(ppDevices);
		}
		MFShutdown();
		return -4;
	}

	// 激活选中的摄像头设备
	hr = ppDevices[index]->ActivateObject(IID_PPV_ARGS(&camera::pSource));
	for (UINT32 i = 0; i < count; i++) ppDevices[i]->Release();
	CoTaskMemFree(ppDevices);

	if (FAILED(hr)) { MFShutdown(); return -5; }

	// 创建 Source Reader，启用视频处理以支持格式转换
	IMFAttributes* pReaderAttr = nullptr;
	hr = MFCreateAttributes(&pReaderAttr, 2);
	if (FAILED(hr)) {
		camera::pSource->Shutdown(); camera::pSource->Release(); camera::pSource = nullptr;
		MFShutdown(); return -6;
	}

	// 启用视频处理：MF 会自动将摄像头输出转换为请求的 RGB32 格式
	hr = pReaderAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
	if (FAILED(hr)) {
		pReaderAttr->Release();
		camera::pSource->Shutdown(); camera::pSource->Release(); camera::pSource = nullptr;
		MFShutdown(); return -7;
	}

	hr = MFCreateSourceReaderFromMediaSource(camera::pSource, pReaderAttr, &camera::pReader);
	pReaderAttr->Release();

	if (FAILED(hr)) {
		camera::pSource->Shutdown(); camera::pSource->Release(); camera::pSource = nullptr;
		MFShutdown(); return -8;
	}

	// 设置 RGB32 输出格式（MF 会自动从 NV12/YUY2 等格式转换）
	IMFMediaType* pMediaType = nullptr;
	hr = MFCreateMediaType(&pMediaType);
	if (FAILED(hr)) { camera::cleanup(); return -9; }

	hr = pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	hr = pMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);

	// 尝试使用摄像头的第一个原生分辨率
	bool setResolution = false;
	for (DWORD i = 0; ; i++) {
		IMFMediaType* pNativeType = nullptr;
		hr = camera::pReader->GetNativeMediaType(
			(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &pNativeType);
		if (FAILED(hr)) break;

		UINT32 w = 0, h = 0;
		MFGetAttributeSize(pNativeType, MF_MT_FRAME_SIZE, &w, &h);
		if (!setResolution && w > 0 && h > 0) {
			MFSetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, w, h);
			setResolution = true;
		}
		pNativeType->Release();
		if (setResolution) break;
	}

	hr = camera::pReader->SetCurrentMediaType(
		(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pMediaType);
	pMediaType->Release();

	if (FAILED(hr)) { camera::cleanup(); return -10; }

	// 获取实际分辨率
	hr = camera::pReader->GetCurrentMediaType(
		(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pMediaType);
	if (SUCCEEDED(hr)) {
		UINT32 w = 0, h = 0;
		MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &w, &h);
		camera::camWidth = (int)w;
		camera::camHeight = (int)h;
		pMediaType->Release();
	}

	camera::isOpen = true;
	return 0;
}

void* cimbar_cam_grab_hbitmap()
{
	if (!camera::isOpen || !camera::pReader) return NULL;

	DWORD streamIndex = 0, flags = 0;
	LONGLONG timestamp = 0;
	IMFSample* pSample = nullptr;

	HRESULT hr = camera::pReader->ReadSample(
		(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
		0, &streamIndex, &flags, &timestamp, &pSample);

	if (FAILED(hr)) return NULL;
	if (!pSample) return NULL;
	// 流结束或流中断，跳过
	if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
		pSample->Release();
		return NULL;
	}

	// 获取连续缓冲区
	IMFMediaBuffer* pBuffer = nullptr;
	hr = pSample->ConvertToContiguousBuffer(&pBuffer);
	pSample->Release();

	if (FAILED(hr) || !pBuffer) return NULL;

	BYTE* pData = nullptr;
	DWORD bufLen = 0;
	hr = pBuffer->Lock(&pData, nullptr, &bufLen);
	if (FAILED(hr)) { pBuffer->Release(); return NULL; }

	int w = camera::camWidth;
	int h = camera::camHeight;
	if (w <= 0 || h <= 0) {
		pBuffer->Unlock(); pBuffer->Release();
		return NULL;
	}

	// 获取帧步幅（每行字节数，可能大于 w*4）
	LONG stride = 0;
	IMFMediaType* pType = nullptr;
	hr = camera::pReader->GetCurrentMediaType(
		(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pType);
	if (SUCCEEDED(hr)) {
		// MF_MT_DEFAULT_STRIDE 属性可能不存在，用 GetUINT32 安全获取
		UINT32 uStride = 0;
		if (SUCCEEDED(pType->GetUINT32(MF_MT_DEFAULT_STRIDE, &uStride)))
			stride = (LONG)uStride;
		pType->Release();
	}
	if (stride == 0) stride = w * 4;

	// 创建 32 位 BGRA HBITMAP（与编码帧格式一致）
	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = w;
	bmi.bmiHeader.biHeight = -h; // 自顶向下
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	HDC hdc = GetDC(NULL);
	void* bits = NULL;
	HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	ReleaseDC(NULL, hdc);

	if (hBitmap && bits) {
		unsigned char* dst = (unsigned char*)bits;
		unsigned char* src = (unsigned char*)pData;
		int dstStride = w * 4;

		if (stride < 0) {
			// 底向上图像，需要翻转行序
			src = src + (h - 1) * (-stride);
			for (int r = 0; r < h; ++r) {
				memcpy(dst + r * dstStride, src - r * (-stride), dstStride);
			}
		} else if (stride == dstStride) {
			memcpy(dst, src, dstStride * h);
		} else {
			// 步幅大于宽度，逐行复制
			for (int r = 0; r < h; ++r) {
				memcpy(dst + r * dstStride, src + r * stride, dstStride);
			}
		}

		// MF RGB32 的 Alpha 通道可能为 0，设为 0xFF 以兼容 GDI+
		for (int i = 0; i < w * h; ++i) {
			dst[i * 4 + 3] = 0xFF;
		}
	}

	pBuffer->Unlock();
	pBuffer->Release();

	return (void*)hBitmap;
}

void cimbar_cam_close()
{
	if (camera::isOpen) {
		camera::cleanup();
	}
}

int cimbar_cam_get_width()
{
	return camera::camWidth;
}

int cimbar_cam_get_height()
{
	return camera::camHeight;
}

} // extern "C"
