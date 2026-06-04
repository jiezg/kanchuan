/* cimbar_cli - libcimbar command-line interface
 *
 * Persistent process: reads commands from stdin, writes responses to stdout.
 * Each command is one line, each response is one line ending with newline.
 * Encoder/decoder state is kept in memory between commands.
 *
 * Commands:
 *   ping                                    -> PONG
 *   quit                                    -> (exit)
 *   encode_file "<path>" [mode] [comp]      -> OK | ERROR <code>
 *   next_frame                              -> OK_MEM <w> <h> <base64> | DONE | ERROR
 *   encode_info                             -> INFO <rec> <cur> <req> <gen>
 *   encode_cleanup                          -> OK
 *   decode_image "<path>" [mode]            -> FILEID <id> | PARTIAL <pct> | EXTRACT_FAILED | ERROR
 *   decode_save <id> "<path>"               -> OK <filename> <size> | ERROR
 *   decode_progress                         -> PROGRESS <s1:pct> <s2:pct> ...
 *   decode_configure [mode]                 -> OK | ERROR
 *   scan_extract "<path>"                   -> OK <size> <base64> | EXTRACT_FAILED | ERROR
 *   fountain_decode <size> (next line=b64)  -> FILEID <id> | OK 0 | ERROR
 *   decode_cleanup                          -> OK
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

static std::ofstream open_file_write(const std::string& path, std::ios_base::openmode mode) {
	std::wstring wpath = utf8_to_wide(path);
	std::filesystem::path fspath(wpath);
	return std::ofstream(fspath, mode);
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
	setvbuf(stdin, NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);

	std::string line;
	while (std::getline(std::cin, line)) {
		if (line.empty()) continue;
		if (!line.empty() && line.back() == '\r') line.pop_back();
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
		/* ===== Encoding Commands ===== */
		else if (cmd == "encode_file") {
			std::string filename = readPath(iss);
			int mode = 68, compression = 16;
			iss >> mode >> compression;
			if (filename.empty()) { std::cout << "ERROR -99 no_filename" << std::endl; continue; }

			int ret = cimbar_encode_file(filename.c_str(), mode, compression);
			if (ret < 0) std::cout << "ERROR " << ret << std::endl;
			else std::cout << "OK" << std::endl;
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
					std::cout << "ERROR -1 bad_frame" << std::endl;
				} else {
					std::vector<unsigned char> buf(size);
					int copied = cimbar_encode_copy_frame(buf.data(), size);
					if (copied <= 0) {
						std::cout << "ERROR " << copied << std::endl;
					} else {
						cv::Mat rgb(h, w, CV_8UC3, buf.data());
						cv::Mat bgr;
						cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
						std::vector<uchar> pngBuf;
						if (!cv::imencode(".png", bgr, pngBuf)) {
							std::cout << "ERROR -2 imencode_failed" << std::endl;
						} else {
							std::string b64 = base64_encode(pngBuf);
							std::cout << "OK_MEM " << w << " " << h << " " << b64 << std::endl;
						}
					}
				}
			}
		}
		else if (cmd == "encode_info") {
			int rec = 0, cur = 0, req = 0, gen = 0;
			int ret = cimbar_encode_get_info(&rec, &cur, &req, &gen);
			if (ret < 0) std::cout << "ERROR " << ret << std::endl;
			else std::cout << "INFO " << rec << " " << cur << " " << req << " " << gen << std::endl;
		}
		else if (cmd == "encode_cleanup") {
			cimbar_encode_cleanup();
			std::cout << "OK" << std::endl;
		}
		/* ===== Decoding Commands - Coarse ===== */
		else if (cmd == "decode_image") {
			std::string imgPath = readPath(iss);
			int mode = 0;
			if (iss >> mode) { /* mode specified */ } else { mode = 0; }
			if (imgPath.empty()) { std::cout << "ERROR -99 no_path" << std::endl; continue; }

			int64_t ret = cimbar_decode_image(imgPath.c_str(), mode);
			if (ret > 0) {
				std::cout << "FILEID " << (uint32_t)ret << std::endl;
			} else if (ret == 0) {
				// Partial decode - report progress
				int numStreams = cimbar_decode_get_num_streams();
				int maxPct = 0;
				for (int i = 0; i < numStreams; i++) {
					int pct = cimbar_decode_get_stream_progress(i);
					if (pct > maxPct) maxPct = pct;
				}
				std::cout << "PARTIAL " << maxPct << std::endl;
			} else if (ret == -1) {
				std::cout << "EXTRACT_FAILED" << std::endl;
			} else {
				std::cout << "ERROR " << ret << std::endl;
			}
		}
		else if (cmd == "decode_save") {
			uint32_t id = 0;
			std::string outputPath;
			iss >> id;
			outputPath = readPath(iss);
			if (outputPath.empty()) { std::cout << "ERROR -99 no_path" << std::endl; continue; }

			char filename[256] = {0};
			cimbar_decode_get_filename(id, filename, sizeof(filename));

			int decBufSize = cimbar_decode_get_decompress_bufsize();
			std::vector<unsigned char> decBuf(decBufSize);
			auto ofs = open_file_write(outputPath, std::ios::binary);
			if (!ofs.is_open()) { std::cout << "ERROR -2 open_failed" << std::endl; continue; }

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
		}
		else if (cmd == "decode_progress") {
			int numStreams = cimbar_decode_get_num_streams();
			std::cout << "PROGRESS";
			for (int i = 0; i < numStreams; i++) {
				int pct = cimbar_decode_get_stream_progress(i);
				std::cout << " " << i << ":" << pct;
			}
			std::cout << std::endl;
		}
		/* ===== Decoding Commands - Fine ===== */
		else if (cmd == "decode_configure") {
			int mode = 68;
			if (iss >> mode) { /* mode specified */ }
			int ret = cimbar_decode_configure(mode);
			if (ret < 0) std::cout << "ERROR " << ret << std::endl;
			else std::cout << "OK" << std::endl;
		}
		else if (cmd == "scan_extract") {
			std::string imgPath = readPath(iss);
			if (imgPath.empty()) { std::cout << "ERROR -99 no_path" << std::endl; continue; }

			// Read image with Unicode path
			std::wstring wpath = utf8_to_wide(imgPath);
			FILE* fp = _wfopen(wpath.c_str(), L"rb");
			if (!fp) { std::cout << "ERROR -2 open_failed" << std::endl; continue; }
			fseek(fp, 0, SEEK_END);
			long fileSize = ftell(fp);
			fseek(fp, 0, SEEK_SET);
			if (fileSize <= 0) { fclose(fp); std::cout << "ERROR -2 empty_file" << std::endl; continue; }
			std::vector<uchar> fileBuf(fileSize);
			fread(fileBuf.data(), 1, fileSize, fp);
			fclose(fp);

			cv::Mat img = cv::imdecode(fileBuf, cv::IMREAD_COLOR);
			if (img.empty()) { std::cout << "ERROR -2 decode_failed" << std::endl; continue; }

			cv::Mat rgb;
			cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);

			int bufsize = cimbar_decode_get_bufsize();
			std::vector<unsigned char> buf(bufsize);
			int ret = cimbar_decode_scan_extract(rgb.data, rgb.cols, rgb.rows, 3, buf.data(), bufsize);
			if (ret < 0) {
				std::cout << "EXTRACT_FAILED " << ret << std::endl;
			} else {
				// Base64 encode the extracted data
				std::vector<unsigned char> data(buf.begin(), buf.begin() + ret);
				std::string b64 = base64_encode(data);
				std::cout << "OK " << ret << " " << b64 << std::endl;
			}
		}
		else if (cmd == "fountain_decode") {
			int size = 0;
			iss >> size;
			if (size <= 0) { std::cout << "ERROR -99 invalid_size" << std::endl; continue; }

			// Read next line for base64 data
			std::string b64line;
			if (!std::getline(std::cin, b64line)) { std::cout << "ERROR -99 no_data" << std::endl; continue; }
			if (!b64line.empty() && b64line.back() == '\r') b64line.pop_back();

			// Base64 decode (simple implementation)
			static const std::string b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::vector<unsigned char> data;
			std::vector<int> T(256, -1);
			for (int i = 0; i < 64; i++) T[b64chars[i]] = i;

			int val = 0, valb = -8;
			for (unsigned char c : b64line) {
				if (T[c] == -1) break;
				val = (val << 6) + T[c];
				valb += 6;
				if (valb >= 0) {
					data.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
					valb -= 8;
				}
			}

			if ((int)data.size() < size) { std::cout << "ERROR -99 insufficient_data" << std::endl; continue; }

			int64_t ret = cimbar_decode_fountain(data.data(), size);
			if (ret > 0) std::cout << "FILEID " << (uint32_t)ret << std::endl;
			else if (ret == 0) std::cout << "OK 0" << std::endl;
			else std::cout << "ERROR " << ret << std::endl;
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
