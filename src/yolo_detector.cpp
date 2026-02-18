#include "yolo_detector.h"
#include "postprocess.h"
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

RKNNDetector::RKNNDetector():ctx(0), model_data(nullptr), model_data_size(0),
                             input_attrs(nullptr), output_attrs(nullptr), model_path(""), width(640), 
                             height(640), channel(3), img_width(0), img_height(0), nms_threshold(NMS_THRESH), 
                             box_conf_threshold(BOX_THRESH) {
}

RKNNDetector::~RKNNDetector(){
    if(model_data){
        free(model_data);
        model_data = nullptr;
    }
    if(input_attrs){
        free(input_attrs);
        input_attrs = nullptr;
    }
    if(output_attrs){
        free(output_attrs);
        output_attrs = nullptr;
    }
    if(ctx){
        rknn_destroy(ctx);
        ctx = 0;
    }
}

/**
 * @brief  从文件加载模型数据
 * @param  filename 模型文件的路径。
 * @param  model_size 指向整数的指针，用于存储模型的大小。
 * @return 指向加载的模型数据的指针，失败时返回nullptr。
 * @note   必须在不再使用这块数据时负责调用free(data) 否则会导致内存泄漏
**/
unsigned char* RKNNDetector::load_model_from_file(const char* filename, int* model_size){
    FILE* fp = fopen(filename, "rb");
    if(fp == nullptr){
        printf("Failed to open model file: %s\n", filename);
        return nullptr;
    }
    // 获取文件大小并读取数据
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char* data = (unsigned char*)malloc(size);
    if(data == nullptr){
        printf("Failed to allocate memory for model\n");
        fclose(fp);
        return nullptr;
    }
    fread(data, 1, size, fp);
    fclose(fp);
    *model_size = size;
    return data;
}

/**
 * @brief  初始化RKNN检测器
 * @param  model_path 模型文件的路径。
 * @return 初始化成功返回0，失败返回-1。
 * @remark 该函数加载模型数据，初始化RKNN上下文，并查询输入输出的数量和属性。
**/
int RKNNDetector::init(const std::string& model_path){
    int ret;

    printf("Initializing RKNNDetector with model: %s...\n", model_path.c_str());
    // 加载模型并初始化RKNN上下文
    model_data = load_model_from_file(model_path.c_str(), &model_data_size);
    if(model_data == nullptr) return -1;

    ret = rknn_init(&ctx, model_data, model_data_size, 0, NULL);
    if(ret < 0){
        printf("rknn_init failed with error code: %d\n", ret);
        free(model_data);
        model_data = nullptr;
        return -1;
    }

    // 获取版本信息
    rknn_sdk_version version;
    rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(version));
    printf("RKNN SDK Version: %s\n", version.api_version);

    // 查询输入输出参数
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if(ret < 0){
        printf("rknn_query RKNN_QUERY_IN_OUT_NUM failed with error code: %d\n", ret);
        free(model_data);
        model_data = nullptr;
        return -1;  
    }
    printf("Model has %d inputs and %d outputs\n", io_num.n_input, io_num.n_output);

    input_attrs = (rknn_tensor_attr*)malloc(sizeof(rknn_tensor_attr) * io_num.n_input);
    output_attrs = (rknn_tensor_attr*)malloc(sizeof(rknn_tensor_attr) * io_num.n_output);
    if(input_attrs == nullptr || output_attrs == nullptr){
        printf("Failed to allocate memory for tensor attributes\n");
        free(model_data);
        model_data = nullptr;
        return -1;
    }

    // 查询输入输出属性
    for(int i=0;i<io_num.n_input;i++){
        input_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attrs[i], sizeof(rknn_tensor_attr));
        if(ret < 0){
            printf("rknn_query RKNN_QUERY_INPUT_ATTR failed with error code: %d\n", ret);
            free(model_data);
            model_data = nullptr;
            return -1;  
        }
    }

    for(int i=0;i<io_num.n_output;i++){
        output_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &output_attrs[i], sizeof(rknn_tensor_attr));
        if(ret < 0){
            printf("rknn_query RKNN_QUERY_OUTPUT_ATTR failed with error code: %d\n", ret);
            free(model_data);
            model_data = nullptr;
            return -1;  
        }
    }

    // 设置输出参数
    if(input_attrs[0].fmt == RKNN_TENSOR_NCHW){
        printf("Model input format is NCHW\n");
        width = input_attrs[0].dims[3];
        height = input_attrs[0].dims[2];
        channel = input_attrs[0].dims[1];
    }
    else{
        printf("Model input format is NHWC\n");
        width = input_attrs[0].dims[2];
        height = input_attrs[0].dims[1];
        channel = input_attrs[0].dims[3];
    }

    return 0;
}

rknn_context *RKNNDetector::get_ctx(){
    return &ctx;
}

/**
 * @brief  执行推理并获取检测结果
 * @param  img_data 输入图像数据，假设为RGB888格式。
 * @param  results 输出检测结果的向量。
 * @return 成功返回0，失败返回-1。
 * @remark 该函数设置输入数据，执行推理，并进行后处理得到检测结果。
 *         需要确保输入图像数据的大小与模型输入尺寸一致。
**/
int RKNNDetector::inference(unsigned char* img_data, std::vector<DetectResult>& results){
    int ret;

    // 设置输入数据 （这里假设模型只有一个输入，且输入格式为NHWC，数据类型为UINT8）
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].buf = img_data; // RGB数据指针
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].pass_through = 0;
    inputs[0].size = input_attrs[0].n_elems * sizeof(uint8_t); // 输入数据大小 这里是INT8

    float scale_w = 1.0f;
    float scale_h = 1.0f;
    BOX_RECT pads;
    memset(&pads, 0, sizeof(BOX_RECT));
    
    rknn_inputs_set(ctx, io_num.n_input, inputs);

    ret = rknn_run(ctx, nullptr);
    if(ret < 0){
        printf("rknn_run failed with error code: %d\n", ret);
        return -1;
    }

    rknn_output outputs[io_num.n_output]; // 存储输出数据的结构体数组 yolov5s模型通常有3个输出
    memset(outputs, 0, sizeof(outputs));
    for(int i=0;i<io_num.n_output;i++){
        outputs[i].index = i;
        outputs[i].want_float = 0;
    }

    ret = rknn_outputs_get(ctx, io_num.n_output, outputs, NULL); // 获取输出数据
    if(ret < 0){
        printf("rknn_outputs_get failed with error code: %d\n", ret);
        return -1;
    }

    // 进行后处理，解析输出数据并填充results
    detect_result_group_t detect_result_group;
    std::vector<float> out_scales;
    std::vector<int32_t> out_zps;
    for(int i=0;i<io_num.n_output;i++)
    {
        out_scales.push_back(output_attrs[i].scale);
        out_zps.push_back(output_attrs[i].zp);
    }
    // 检查输出向量维度是否符合预期，yolov5s模型通常有3个输出，分别对应不同尺度的检测结果
    if(io_num.n_output < 3){
        printf("Unexpected number of outputs: %d\n", io_num.n_output);
        rknn_outputs_release(ctx, io_num.n_output, outputs); // 释放之前的输出数据
        return -1;
    }

    post_process((int8_t*)outputs[0].buf, (int8_t*)outputs[1].buf, (int8_t*)outputs[2].buf, height, width,
                  box_conf_threshold, nms_threshold, pads, scale_w, scale_h, out_zps, out_scales, &detect_result_group);

    results.clear();
    for(int i=0;i<detect_result_group.count;i++){
        DetectResult res;
        res.id = detect_result_group.results[i].class_index;
        res.name = detect_result_group.results[i].name;
        res.confidence = detect_result_group.results[i].prop;
        res.box.left = detect_result_group.results[i].box.left;
        res.box.top = detect_result_group.results[i].box.top;
        res.box.right = detect_result_group.results[i].box.right;
        res.box.bottom = detect_result_group.results[i].box.bottom;
        results.push_back(res);
    }
    
    rknn_outputs_release(ctx, io_num.n_output, outputs); // 释放之前的输出数据

    return 0;
}
