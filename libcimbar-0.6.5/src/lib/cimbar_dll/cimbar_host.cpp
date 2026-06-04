/* cimbar_host - 64-bit bridge for aardio (32-bit)
 *
 * Persistent process: reads commands from stdin, writes responses to stdout.
 * Each command is one line, each response is one line ending with newline.
 * Encoder/decoder state is kept in memory between commands.
 *
 * Commands:
 *   encode_file "<filename>" <mode> <compression>
 *     -> OK | ERROR <code>
 *   next_frame
 *     -> OK_MEM <width> <height> <base64_png> | DONE | ERROR <code>
 *   encode_cleanup
 *     -> OK
 *   decode_configure <mode>
 *     -> OK | ERROR <code>
 *   decode_frame "<img_path>" "<data_path>"
 *     -> OK <size> | EXTRACT_FAILED <code> | ERROR <code>
 *   decode_fountain "<data_path>"
 *     -> FILEID <id> | OK 0 | ERROR <code>
 *   decode_save <id> "<output_path>"
 *     -> OK <filename> <size> | ERROR <code>
 *   decode_cleanup
 *     -> OK
 *   ping
 *     -> PONG
 *   quit
 *     (no response, exit)
 */

#include "cimbar_dll.h"
#include <opencv2/opencv.hpp>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <iomanip>

/* ===== Base64 Encoding ===== */
static const char base64_chars[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const std::vector<unsigned char>& data) {
	std::string result;
	result.reserve(((data.size() + 2) / 3) * 4);

	size_t i = 0;
	for (; i + 2 < data.size(); i += 3) {
		result += base64_chars[(data[i] >> 2) & 0x3F];
		result += base64_chars[((data[i] & 0x3) << 4) | ((data[i+1] >> 4) & 0xF)];
		result += base64_chars[((data[i+1] & 0xF) << 2) | ((data[i+2] >> 6) & 0x3)];
		result += base64_chars[data[i+2] & 0x3F];
	}

	if (i < data.size()) {
		result += base64_chars[(data[i] >> 2) & 0x3F];
		if (i + 1 < data.size()) {
			result += base64_chars[((data[i] & 0x3) << 4) | ((data[i+1] >> 4) & 0xF)];
			result += base64_chars[((data[i+1] & 0xF) << 2)];
		} else {
			result += base64_chars[((data[i] & 0x3) << 4)];
			result += '=';
		}
		result += '=';
	}

	return result;
}

/* ===== Unicode Path Helpers ===== */
static std::wstring utf8_to_wide(const std::string& str) {
	if (str.empty()) return std::wstring();
	int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	if (size <= 0) return std::wstring();
	std::wstring result(size - 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
	return result;
}

static std::ifstream open_file_read(const std::string& path, std::ios_base::openmode mode) {
	std::wstring wpath = utf8_to_wide(path);
	std::filesystem::path fspath(wpath);
	return std::ifstream(fspath, mode);
}

static std::ofstream open_file_write(const std::string& path, std::ios_base::openmode mode) {
	std::wstring wpath = utf8_to_wide(path);
	std::filesystem::path fspath(wpath);
	return std::ofstream(fspath, mode);
}

// cv::imread with Unicode path support
static cv::Mat imread_unicode(const std::string& path, int flags = cv::IMREAD_COLOR) {
	std::wstring wpath = utf8_to_wide(path);
	FILE* fp = _wfopen(wpath.c_str(), L"rb");
	if (!fp) return cv::Mat();

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (size <= 0) { fclose(fp); return cv::Mat(); }

	std::vector<uchar> buf(size);
	fread(buf.data(), 1, size, fp);
	fclose(fp);

	return cv::imdecode(buf, flags);
}

// Read a quoted or unquoted string from stream
static std::string readPath(std::istringstream& iss) {
	std::string path;
	iss >> std::ws;
	if (iss.peek() == '"') {
		iss.get();
		std::getline(iss, path, '"');
	} else {
		iss >> path;
	}
	return path;
}

int main() {
	// Set stdin/stdout to unbuffered for reliable pipe communication
	setvbuf(stdin, NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);

	std::string line;
	while (std::getline(std::cin, line)) {
		if (line.empty()) continue;

		// Trim trailing \r in case of Windows line endings
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty()) continue;

		std::istringstream iss(line);
		std::string cmd;
		iss >> cmd;

		if (cmd == "quit") {
			cimbar_encode_cleanup();
			cimbar_decode_cleanup();
			break;
		}
		else if (cmd == "ping") {
			std::cout << "PONG" << std::endl;
		}
		else if (cmd == "encode_file") {
			std::string filename = readPath(iss);
			int mode = 68, compression = 16;
			iss >> mode >> compression;

			if (filename.empty()) {
				std::cout << "ERROR -99 no_filename" << std::endl;
				continue;
			}

			int ret = cimbar_encode_file(filename.c_str(), mode, compression);
			if (ret < 0) {
				std::cout << "ERROR " << ret << std::endl;
			} else {
				std::cout << "OK" << std::endl;
			}
		}
		else if (cmd == "next_frame") {
			int frameNum = cimbar_encode_next_frame();
			if (frameNum <= 0) {
				std::cout << "DONE" << std::endl;
			} else {
				int w = cimbar_encode_get_frame_width();
				int h = cimbar_encode_get_frame_height();
				int size = cimbar_encode_get_frame_size();
				if (w <= 0 || h <= 0 || size <= 0) {
					std::cout << "ERROR -1 bad_frame_dims" << std::endl;
				} else {
					std::vector<unsigned char> buf(size);
					int copied = cimbar_encode_copy_frame(buf.data(), size);
					if (copied <= 0) {
						std::cout << "ERROR " << copied << std::endl;
					} else {
						// Encoder produces RGB data, imencode expects BGR
						cv::Mat rgb(h, w, CV_8UC3, buf.data());
						cv::Mat bgr;
						cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);

						// Encode as PNG in memory
						std::vector<uchar> pngBuf;
						if (!cv::imencode(".png", bgr, pngBuf)) {
							std::cout << "ERROR -2 imencode_failed" << std::endl;
						} else {
							// Base64 encode and send inline
							std::string b64 = base64_encode(pngBuf);
							std::cout << "OK_MEM " << w << " " << h << " " << b64 << std::endl;
						}
					}
				}
			}
		}
		else if (cmd == "encode_cleanup") {
			cimbar_encode_cleanup();
			std::cout << "OK" << std::endl;
		}
		else if (cmd == "decode_configure") {
			int mode = 68;
			iss >> mode;
			int ret = cimbar_decode_configure(mode);
			if (ret < 0) {
				std::cout << "ERROR " << ret << std::endl;
			} else {
				std::cout << "OK" << std::endl;
			}
		}
		else if (cmd == "decode_frame") {
			std::string imgPath = readPath(iss);
			std::string dataPath = readPath(iss);

			if (imgPath.empty() || dataPath.empty()) {
				std::cout << "ERROR -99 missing_args" << std::endl;
				continue;
			}

			cv::Mat img = imread_unicode(imgPath, cv::IMREAD_COLOR);
			if (img.empty()) {
				std::cout << "ERROR -2 imread_failed" << std::endl;
			} else {
				cv::Mat rgb;
				cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);

				int bufsize = cimbar_decode_get_bufsize();
				std::vector<unsigned char> buf(bufsize);
				int ret = cimbar_decode_scan_extract(rgb.data, rgb.cols, rgb.rows, 3, buf.data(), bufsize);
				if (ret < 0) {
					std::cout << "EXTRACT_FAILED " << ret << std::endl;
				} else {
					auto ofs = open_file_write(dataPath, std::ios::binary);
					ofs.write(reinterpret_cast<const char*>(buf.data()), ret);
					ofs.close();
					std::cout << "OK " << ret << std::endl;
				}
			}
			// Do NOT cleanup here - fountain decoder must accumulate across frames
		}
		else if (cmd == "decode_fountain") {
			std::string dataPath = readPath(iss);

			if (dataPath.empty()) {
				std::cout << "ERROR -99 no_path" << std::endl;
				continue;
			}

			auto ifs = open_file_read(dataPath, std::ios::binary | std::ios::ate);
			if (!ifs.is_open()) {
				std::cout << "ERROR -1 open_failed" << std::endl;
			} else {
				int dataSize = (int)ifs.tellg();
				ifs.seekg(0, std::ios::beg);
				std::vector<unsigned char> buf(dataSize);
				ifs.read(reinterpret_cast<char*>(buf.data()), dataSize);
				ifs.close();

				int64_t ret = cimbar_decode_fountain(buf.data(), dataSize);
				if (ret < 0) {
					std::cout << "ERROR " << ret << std::endl;
				} else if (ret > 0) {
					std::cout << "FILEID " << (uint32_t)ret << std::endl;
				} else {
					std::cout << "OK 0" << std::endl;
				}
			}
		}
		else if (cmd == "decode_save") {
			uint32_t id = 0;
			std::string outputPath;
			iss >> id;
			outputPath = readPath(iss);

			if (outputPath.empty()) {
				std::cout << "ERROR -99 no_path" << std::endl;
				continue;
			}

			char filename[256] = {0};
			cimbar_decode_get_filename(id, filename, sizeof(filename));

			// Loop decompression for large files
			int decBufSize = cimbar_decode_get_decompress_bufsize();
			std::vector<unsigned char> decBuf(decBufSize);
			auto ofs = open_file_write(outputPath, std::ios::binary);
			if (!ofs.is_open()) {
				std::cout << "ERROR -2 open_output_failed" << std::endl;
				continue;
			}

			int totalSize = 0;
			int decSize;
			while ((decSize = cimbar_decode_decompress_read(id, decBuf.data(), decBufSize)) > 0) {
				ofs.write(reinterpret_cast<const char*>(decBuf.data()), decSize);
				totalSize += decSize;
			}
			ofs.close();

			if (totalSize == 0 && decSize < 0) {
				std::cout << "ERROR " << decSize << std::endl;
			} else {
				std::cout << "OK " << filename << " " << totalSize << std::endl;
			}
			// Do NOT cleanup here - user may want to save again or continue decoding
		}
		else if (cmd == "decode_cleanup") {
			cimbar_decode_cleanup();
			std::cout << "OK" << std::endl;
		}
		else {
			std::cout << "ERROR -99 unknown_command" << std::endl;
		}
	}

	return 0;
}
