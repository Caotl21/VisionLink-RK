#ifndef RKNN_DETECTOR_H
#define RKNN_DETECTOR_H

#include "../3rdparty/rknpu2/include/rknn_api.h"
#include <vector>
#include <string>

// 定义检测结果结构体 (和 postprocess.h 里保持一致最好)
typedef struct {
    int id;
    std::string name;   
    float confidence;
    struct {
        int left, top, right, bottom;
    } box;
} DetectResult;

class RKNNDetector{
public:
    RKNNDetector();
    ~RKNNDetector();

    int init(const std::string& model_path);
    int inference(unsigned char* img_data, std::vector<DetectResult>& results);
    rknn_context *get_ctx();

private:
    rknn_context ctx;   // RKNN上下文句柄
    unsigned char* model_data;
    int model_data_size;
    
    std::string model_path;

    rknn_input_output_num io_num;   // 输入输出数量
    rknn_tensor_attr* input_attrs;  // 输入属性
    rknn_tensor_attr* output_attrs; // 输出属性

    float nms_threshold, box_conf_threshold;

    int channel, width, height; // 模型输入尺寸
    int img_width, img_height; // 原始图像尺寸

    unsigned char* load_model_from_file(const char* filename, int* model_size);
};

#endif // RKNN_DETECTOR_H