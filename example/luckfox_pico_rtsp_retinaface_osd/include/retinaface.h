// Copyright (c) 2023 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#ifndef _RKNN_DEMO_RETINAFACE_RV1106_H_
#define _RKNN_DEMO_RETINAFACE_RV1106_H_

#include "rknn_api.h"

#include <stdint.h>
#include <vector>
#include "rknn_api.h"

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>


typedef struct {
    char *dma_buf_virt_addr;
    int dma_buf_fd;
    int size;
}rknn_dma_buf;


typedef struct {
    int left;
    int top;
    int right;
    int bottom;
}image_rect_t; 

typedef struct {
    int x;
    int y;
}point_t;

typedef struct {
    image_rect_t box;
    float prop;
    point_t point[5];
} object_detect_result;

typedef struct {
    //int id;
    int count;
    object_detect_result results[128];
} object_detect_result_list;

typedef struct {
    rknn_context rknn_ctx;
    rknn_tensor_mem* max_mem;
    rknn_tensor_mem* net_mem;
    rknn_input_output_num io_num;
    rknn_tensor_attr* input_attrs;
    rknn_tensor_attr* output_attrs;
    rknn_tensor_mem* input_mems[1];
    rknn_tensor_mem* output_mems[3];
    int model_channel;
    int model_width;
    int model_height;
    
    bool is_quant;
} rknn_app_context_t;



//retinaface
int init_retinaface_model(const char* model_path, rknn_app_context_t* app_ctx);
int release_retinaface_model(rknn_app_context_t* app_ctx);
int inference_retinaface_model(rknn_app_context_t* app_ctx,object_detect_result_list* od_results);



#endif 