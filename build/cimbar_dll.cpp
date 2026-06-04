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
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

/* ===== Unicode Path Helper ===== */
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

	cimbar::Config::update(mode_val);
	encode::modeVal = mode_val;

	if (compression < 0 || compression > 22)
		compression = cimbar::Config::compression_level();
	encode::compressionLevel = compression;

	if (!FountainInit::init())
		return -5;

	std::ifstream ifs = open_file_unicode(filename, std::ios::binary);
	if (!ifs.is_open())
		return -2;

	auto comp = std::make_unique<cimbar::zstd_compressor<std::stringstream>>();
	if (!comp)
		return -3;

	comp->set_compression_level(compression);

	std::string fn = File::basename(filename);
	if (!fn.empty())
		comp->write_header(fn.data(), fn.size());

	if (!comp->compress(ifs))
		return -4;

	unsigned fountainChunkSize = cimbar::Config::fountain_chunk_size();
	size_t compressedSize = comp->size();
	if (compressedSize < fountainChunkSize)
		comp->pad(fountainChunkSize - compressedSize + 1);

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
				dstRow[c * 4 + 0] = srcRow[c * 3 + 2];
				dstRow[c * 4 + 1] = srcRow[c * 3 + 1];
				dstRow[c * 4 + 2] = srcRow[c * 3 + 0];
				dstRow[c * 4 + 3] = 0xFF;
			}
		}
		else if (channels == 4)
		{
			for (int c = 0; c < w; ++c)
			{
				dstRow[c * 4 + 0] = srcRow[c * 4 + 2];
				dstRow[c * 4 + 1] = srcRow[c * 4 + 1];
				dstRow[c * 4 + 2] = srcRow[c * 4 + 0];
				dstRow[c * 4 + 3] = 0xFF;
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

	if (mode_val > 0)
		cimbar_decode_configure(mode_val);

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

	cv::Mat rgb;
	cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);

	int bufsize = cimbar_decode_get_bufsize();
	std::vector<unsigned char> bufspace(bufsize);
	int bytesDecoded = cimbar_decode_scan_extract(rgb.data, rgb.cols, rgb.rows, 3, bufspace.data(), bufsize);
	if (bytesDecoded < 0)
		return -1;

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

} // extern "C"
