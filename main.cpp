#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <string>
#include <zlib.h>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <sys/stat.h>
#include <algorithm> // 添加 max 函数支持
#include <cctype>    // 添加 tolower 支持

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

// 解析帧范围参数
bool parseFrameRange(const std::string& range, size_t maxFrames, 
                     size_t& start, size_t& end, size_t& step) {
    std::istringstream ss(range);
    char colon;
    
    start = 0;
    end = maxFrames - 1;
    step = 1;
    
    if (!(ss >> start)) return false;
    if (ss >> colon && colon == ':' && ss >> end) {
        if (ss >> colon && colon == ':' && ss >> step) {
            // start:end:step
        } else {
            // start:end
            step = 1;
        }
    } else {
        // 单个帧
        end = start;
        step = 1;
    }
    
    // 边界检查
    if (start > end) std::swap(start, end);
    if (start >= maxFrames) start = 0;
    if (end >= maxFrames) end = maxFrames - 1;
    if (step == 0) step = 1;
    
    return true;
}

// 获取文件大小
uint64_t getFileSize(const std::string& filename) {
    struct stat stat_buf;
    if (stat(filename.c_str(), &stat_buf) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(stat_buf.st_size);
}

// 文件存在检查
bool fileExists(const std::string& filename) {
    std::ifstream f(filename.c_str());
    return f.good();
}

int main(int argc, char* argv[]) {
    // 检查参数
    if (argc < 4) {
        std::cerr << "用法: " << argv[0] << " <yuv文件> <宽度> <高度> [帧范围] [测试次数]\n";
        std::cerr << "帧范围格式: [起始帧][:结束帧][:步长]\n";
        std::cerr << "示例: " << argv[0] << " test.yuv 1920 1080\n";
        std::cerr << "      测试文件的第一帧\n";
        std::cerr << "示例: " << argv[0] << " test.yuv 1920 1080 10\n";
        std::cerr << "      测试第10帧(0-based)\n";
        std::cerr << "示例: " << argv[0] << " test.yuv 1920 1080 0:9\n";
        std::cerr << "      测试前10帧\n";
        std::cerr << "示例: " << argv[0] << " test.yuv 1920 1080 0:99:10\n";
        std::cerr << "      测试前100帧，每隔10帧(共10帧)\n";
        std::cerr << "示例: " << argv[0] << " test.yuv 1920 1080 0:99 3\n";
        std::cerr << "      测试前100帧，重复3次取平均值\n";
        return 1;
    }

    const std::string filename = argv[1];
    const int width = std::stoi(argv[2]);
    const int height = std::stoi(argv[3]);
    const int frameSize = width * height * 3 / 2;  // YUV420P大小

    // 默认帧范围
    std::string frameRange = "0";
    int testRuns = 1;
    
    // 解析可选参数
    if (argc > 4) {
        frameRange = argv[4];
    }
    if (argc > 5) {
        testRuns = std::max(1, std::stoi(argv[5]));
    }

    // 检查文件是否存在
    if (!fileExists(filename)) {
        std::cerr << "错误: 文件不存在 - " << filename << "\n";
        return 1;
    }
    
    // 获取文件大小并计算帧数
    const uint64_t fileSize = getFileSize(filename);
    if (fileSize < static_cast<uint64_t>(frameSize)) {
        std::cerr << "错误: 文件太小(需至少一帧)\n"
                  << "要求: " << frameSize << " 字节, 实际: " << fileSize << " 字节\n";
        return 1;
    }
    
    const size_t maxFrames = fileSize / frameSize;
    size_t startFrame, endFrame, frameStep;
    if (!parseFrameRange(frameRange, maxFrames, startFrame, endFrame, frameStep)) {
        std::cerr << "错误: 无效的帧范围格式: " << frameRange << "\n";
        return 1;
    }
    
    // 计算实际测试帧数
    const size_t frameCount = (endFrame - startFrame) / frameStep + 1;
    
    // 打开YUV文件
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "无法打开文件: " << filename << "\n";
        return 1;
    }
    
    // 显示基本信息
    std::cout << "=== YUV压缩测试 ===\n";
    std::cout << "文件: " << filename << "\n";
    std::cout << "分辨率: " << width << "x" << height << "\n";
    std::cout << "帧大小: " << (frameSize >> 10) << " KB\n";
    std::cout << "文件大小: " << (fileSize >> 20) << " MB\n";
    std::cout << "总帧数: " << maxFrames << "\n";
    std::cout << "测试帧范围: " << startFrame << ":" << endFrame << ":" << frameStep << "\n";
    std::cout << "测试帧数: " << frameCount << "\n";
    std::cout << "测试次数: " << testRuns << "\n";
    
    // 加载测试帧到内存
    std::vector<std::vector<uint8_t>> frames;
    frames.reserve(frameCount);
    
    // 文件定位到起始帧
    file.seekg(startFrame * frameSize, std::ios::beg);
    
    for (size_t i = 0; i < frameCount; i++) {
        std::vector<uint8_t> frame(frameSize);
        file.read(reinterpret_cast<char*>(frame.data()), frameSize);
        
        // 跳过中间帧
        if (frameStep > 1 && i < frameCount - 1) {
            file.seekg((frameStep - 1) * frameSize, std::ios::cur);
        }
        
        frames.push_back(std::move(frame));
    }
    file.close();
    
    std::cout << "已加载 " << frames.size() << " 帧到内存\n\n";
    
    // 压缩级别测试
    const int levels[] = { Z_BEST_SPEED, 3, 5, 7, Z_BEST_COMPRESSION };
    const char* levelNames[] = { "最快速度", "3", "5", "7", "最佳压缩"};
    
    // 各压缩级别的统计数据
    for (int l = 0; l < sizeof(levels)/sizeof(levels[0]); l++) {
        const int level = levels[l];
        
        std::cout << "------------------------------------------\n";
        std::cout << "压缩级别: " << levelNames[l] << " (ZLib级别 " << level << ")\n";
        std::cout << "------------------------------------------\n";
        
        // 单帧统计
        double totalTime = 0;
        double totalCompressedSize = 0;
        double minRatio = 1000.;
        double maxRatio = 0;
        double minSpeed = 1000.;
        double maxSpeed = 0;
        int errorCount = 0;
        
        // 多次测试取平均值
        for (int run = 0; run < testRuns; run++) {
            for (size_t i = 0; i < frameCount; i++) {
                std::vector<uint8_t> compressed;
                double time = 0;
                
                try {
                    // 预热一次
                    if (run == 0 && i == 0) {
                        std::vector<uint8_t> warmup;
                        compressYUV420P(frames[i], warmup, level);
                    }
                    
                    // 压缩当前帧
                    time = compressYUV420P(frames[i], compressed, level);
                    
                    // 计算统计值
                    const double ratio = static_cast<double>(frames[i].size()) / compressed.size();
                    const double speed = (frames[i].size() / (1024.0 * 1024.0)) / (time / 1000.0);
                    
                    // 更新统计
                    totalTime += time;
                    totalCompressedSize += compressed.size();
                    
                    // 更新极值
                    if (ratio < minRatio) minRatio = ratio;
                    if (ratio > maxRatio) maxRatio = ratio;
                    if (speed < minSpeed) minSpeed = speed;
                    if (speed > maxSpeed) maxSpeed = speed;
                    
                    // 输出单帧结果（只在第一次测试时输出）
                    if (testRuns == 1) {
                        std::cout << "帧 " << (startFrame + i * frameStep) << ": "
                                  << "时间=" << std::fixed << std::setprecision(2) << time << "ms, "
                                  << "压缩比=" << std::fixed << std::setprecision(2) << ratio << ":1, "
                                  << "速度=" << std::fixed << std::setprecision(2) << speed << " MB/s, "
                                  << "压缩后大小=" << (compressed.size() >> 10) << " KB\n";
                    }
                } catch (const std::exception& e) {
                    errorCount++;
                    if (testRuns == 1) {
                        std::cerr << "帧 " << (startFrame + i * frameStep) << " 压缩失败: " << e.what() << "\n";
                    }
                }
            }
        }
        
        // 计算平均值（如果多轮测试）
        const double avgTime = totalTime / (testRuns * frameCount);
        const double avgCompressedSize = totalCompressedSize / (testRuns * frameCount);
        const double avgRatio = frames.size() > 0 ? 
            (frameSize * frameCount) / (avgCompressedSize * frameCount) : 0;
        const double avgSpeed = frameSize > 0 ? 
            (frameSize / (1024.0 * 1024.0)) / (avgTime / 1000.0) : 0;
        const double spaceSaving = (1.0 - 1.0 / avgRatio) * 100.0;
        
        // 输出总体结果
        std::cout << "\n总体统计(" << (testRuns * frameCount) << "次测试):\n";
        std::cout << "  平均压缩时间: " << std::fixed << std::setprecision(2) << avgTime << " ms/帧\n";
        std::cout << "  平均压缩率: " << std::fixed << std::setprecision(2) << avgRatio << ":1 (最小: " 
                  << std::setprecision(2) << minRatio << ", 最大: " << maxRatio << ")\n";
        std::cout << "  平均速度: " << std::fixed << std::setprecision(2) << avgSpeed << " MB/s (最小: " 
                  << std::setprecision(2) << minSpeed << ", 最大: " << maxSpeed << ")\n";
        std::cout << "  空间节省: " << std::fixed << std::setprecision(1) << spaceSaving << "%\n";
        std::cout << "  原始大小: " << (frameSize >> 10) << " KB/帧\n";
        std::cout << "  压缩后大小: " << std::fixed << std::setprecision(1) << (avgCompressedSize / 1024.0) << " KB/帧\n";
        
        if (errorCount > 0) {
            std::cout << "  错误帧数: " << errorCount << "\n";
        }
        std::cout << "\n";
    }
    
    return 0;
}