#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <string>
#include <zlib.h>
#include <cctype>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

// 压缩YUV420P数据
double compressYUV420P(const std::vector<uint8_t>& input, std::vector<uint8_t>& output, int level) {
    uLongf compressedSize = compressBound(static_cast<uLong>(input.size()));
    output.resize(compressedSize);
    
    auto start = std::chrono::high_resolution_clock::now();
    int ret = compress2(output.data(), &compressedSize,
        input.data(), static_cast<uLong>(input.size()), level);
    auto end = std::chrono::high_resolution_clock::now();
    
    if (ret != Z_OK) {
        throw std::runtime_error("压缩失败: " + std::to_string(ret));
    }
    
    output.resize(compressedSize);
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// 跨平台文件大小获取
uint64_t getFileSize(const std::string& filename) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(filename.c_str(), GetFileExInfoStandard, &fad)) {
        return 0;
    }
    LARGE_INTEGER size;
    size.HighPart = fad.nFileSizeHigh;
    size.LowPart = fad.nFileSizeLow;
    return size.QuadPart;
#else
    struct stat stat_buf;
    if (stat(filename.c_str(), &stat_buf) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(stat_buf.st_size);
#endif
}

// 跨平台文件存在检查
bool fileExists(const std::string& filename) {
#ifdef _WIN32
    return GetFileAttributesA(filename.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    std::ifstream f(filename.c_str());
    return f.good();
#endif
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "用法: " << argv[0] << " <yuv文件> <宽度> <高度>\n";
        std::cerr << "示例: " << argv[0] << " test.yuv 1920 1080\n";
        return 1;
    }

    const std::string filename = argv[1];
    const int width = std::stoi(argv[2]);
    const int height = std::stoi(argv[3]);
    const int frameSize = width * height * 3 / 2;  // YUV420P大小

    // 检查文件是否存在
    if (!fileExists(filename)) {
        std::cerr << "错误: 文件不存在 - " << filename << "\n";
        return 1;
    }
    
    // 获取文件大小
    const uint64_t fileSize = getFileSize(filename);
    if (fileSize < static_cast<uint64_t>(frameSize)) {
        std::cerr << "错误: 文件太小(需至少一帧)\n"
                  << "要求: " << frameSize << " 字节, 实际: " << fileSize << " 字节\n";
        return 1;
    }
    
    // 读取第一帧
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "无法打开文件: " << filename << "\n";
        return 1;
    }
    
    std::vector<uint8_t> yuvData(frameSize);
    file.read(reinterpret_cast<char*>(yuvData.data()), frameSize);
    file.close();
    
    // 显示基本信息
    std::cout << "=== YUV压缩测试 ===\n";
    std::cout << "文件: " << filename << "\n";
    std::cout << "分辨率: " << width << "x" << height << "\n";
    std::cout << "帧大小: " << (frameSize >> 20) << " MB (" << (frameSize / 1024) << " KB)\n";
    std::cout << "文件大小: " << (fileSize >> 20) << " MB (" << (fileSize / 1024) << " KB)\n\n";
    
    // 压缩级别测试
    const int levels[] = { Z_NO_COMPRESSION, Z_BEST_SPEED, Z_DEFAULT_COMPRESSION, Z_BEST_COMPRESSION };
    const char* levelNames[] = { "无压缩", "最快速度", "默认", "最佳压缩" };
    
    for (int i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        int level = levels[i];
        std::vector<uint8_t> compressed;
        double time = 0;
        
        // 预热一次
        try {
            std::vector<uint8_t> warmup;
            compressYUV420P(yuvData, warmup, level);
            
            // 多次测试取平均
            const int testRuns = 5;
            for (int i = 0; i < testRuns; i++) {
                time += compressYUV420P(yuvData, compressed, level);
            }
            time /= testRuns;
            
            double ratio = static_cast<double>(yuvData.size()) / compressed.size();
            double speed = (yuvData.size() / (1024.0 * 1024.0)) / (time / 1000.0);
            
            // 输出结果表格
            std::cout << "级别: " << levelNames[i] << "\n";
            std::cout << "  压缩率: " << std::fixed << ratio << ":1\n";
            std::cout << "  压缩时间: " << std::fixed << time << " ms\n";
            std::cout << "  原始大小: " << (yuvData.size() / (1024.0 * 1024.0)) << " MB\n";
            std::cout << "  压缩后大小: " << (compressed.size() / (1024.0 * 1024.0)) << " MB\n";
            std::cout << "  空间节省: " << std::fixed 
                      << (1.0 - 1.0 / ratio) * 100.0 << "%\n";
            std::cout << "  速度: " << std::fixed << speed << " MB/s\n\n";
        } catch (const std::exception& e) {
            std::cerr << levelNames[i] << " 压缩失败: " << e.what() << "\n\n";
        }
    }
    
    return 0;
}