#!/home/AUV1/miniconda3/envs/yolov5_ROS/bin/python3.10
import sys
import os

import cv2
import numpy as np
import time
import rospy
from sensor_msgs.msg import Image
from cv_bridge import CvBridge
from std_msgs.msg import Header, Float32
import json
import threading
import logging

from yolov5_rknn import RKNN_Inference

HAS_DISPLAY = bool(os.environ.get("DISPLAY"))

# yolov5配置参数 .yaml中的参数
RKNN_MODEL = 'yolov5s.rknn'  # 请修改为你的RKNN模型路径
IMG_WIDTH, IMG_HEIGHT = 640, 640 # 模型输入尺寸
IMG_SIZE = 640
OBJ_THRESH = 0.25
NMS_THRESH = 0.45
CLASSES = ("person", "bicycle", "car", "motorbike ", "aeroplane ", "bus ", "train", "truck ", "boat", "traffic light",
           "fire hydrant", "stop sign ", "parking meter", "bench", "bird", "cat", "dog ", "horse ", "sheep", "cow", "elephant",
           "bear", "zebra ", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard",
           "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle",
           "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange", "broccoli",
           "carrot", "hot dog", "pizza", "donut", "cake", "chair", "sofa", "pottedplant", "bed", "diningtable", "toilet",
           "tvmonitor", "laptop", "mouse", "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
           "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush")


def ensure_logging_integrity():
    # 检查标准级别是否存在
    if logging.getLevelName("DEBUG") != 10:
        print("警告: 检测到 logging 模块被篡改，正在尝试修复...")
        
        # 强制添加回标准级别
        logging.addLevelName(10, "DEBUG")
        logging.addLevelName(20, "INFO")
        logging.addLevelName(30, "WARNING")
        logging.addLevelName(40, "ERROR")
        logging.addLevelName(50, "CRITICAL")

class ROSInterface:
    """
    ROS接口层，不改变原有算法逻辑
    只负责接收原始图像，调用原有算法，发布结果
    """
    def __init__(self):
        # ROS初始化
        print("初始化ROS节点...")
        rospy.init_node('yolov5_rknn_ros', anonymous=True)
        print("ROS节点初始化完成")
        
        # 初始化CV Bridge
        self.bridge = CvBridge()
        # 加载模型（原有逻辑）
        self.rknn_lite = RKNN_Inference(RKNN_MODEL)
        
        # ============== ROS话题定义 ==============
        # 发布者：带标注的图像
        self.annotated_pub = rospy.Publisher('/yolo/detection_image',Image,queue_size=10)
        # # 发布者：检测结果（JSON格式字符串）
        self.result_pub = rospy.Publisher('/yolo/detection_results',Image, queue_size=10) # 临时使用Image消息，实际上会发布JSON文本
        # # 发布者：边界框可视化（用于rviz）
        self.bbox_pub = rospy.Publisher('/yolo/bounding_boxes',Image,queue_size=10)
        
        # 性能统计
        self.frame_count = 0
        self.last_time = time.time()
        rospy.loginfo("YOLOv5 ROS接口已启动")
    
    def run(self):
        """
        ROS图像回调函数
        这里调用原有的YOLOv5算法，然后发布结果
        """
        #打开摄像头
        cap = cv2.VideoCapture(0)
        # 设置摄像头分辨率 (可选)
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        if not cap.isOpened():
           # print("无法打开摄像头")
            return
        #print("开始推理，按 'q' 键退出")
        fps_time = time.time()
        frame_count = 0
        try:
            while not rospy.is_shutdown():
                ret, frame = cap.read()
                if not ret:
                    #print("无法读取摄像头帧")
                    break
            
                # 2. 调用原有推理逻辑（完全不变）
                start_time = time.time()
                outputs = self.rknn_lite.run(frame)
                inference_time = (time.time() - start_time) * 1000
            
                # 3. 调用原有后处理逻辑（完全不变）
                input0_data = outputs[0]
                input1_data = outputs[1]
                input2_data = outputs[2]

                input0_data = input0_data.reshape([3, -1] + list(input0_data.shape[-2:]))
                input1_data = input1_data.reshape([3, -1] + list(input1_data.shape[-2:]))
                input2_data = input2_data.reshape([3, -1] + list(input2_data.shape[-2:]))

                input_data = list()
                input_data.append(np.transpose(input0_data, (2, 3, 0, 1)))
                input_data.append(np.transpose(input1_data, (2, 3, 0, 1)))
                input_data.append(np.transpose(input2_data, (2, 3, 0, 1)))
                
                # 调用原有的后处理函数
                boxes, classes, scores = self.rknn_lite.yolov5_post_process(input_data)
            
                # 4. 计算FPS
                self.frame_count += 1
                if frame_count >= 30:
                    fps = 30 / (time.time() - fps_time)
                    fps_time = time.time()
                    frame_count = 0
                    rospy.loginfo_throttle(f"FPS: {fps:.2f}, Inference: {inference_time:.2f}ms")
            
                img_1 = cv2.cvtColor(self.rknn_lite.img_rgb, cv2.COLOR_RGB2BGR)
                if boxes is not None:
                    self.rknn_lite.draw(img_1, boxes, scores, classes)
                    header = Header()
                    header.seq = self.frame_count
                    header.stamp = rospy.Time.now()
                    header.frame_id = "camera"
                    self.frame_count += 1
                    ros_image = self.bridge.cv2_to_imgmsg(img_1, "bgr8")
                    ros_image.header = header
                    self.annotated_pub.publish(ros_image)
                # show output
                #cv2.imshow("post process result", img_1)

                #if cv2.waitKey(1) == ord('q'):
                #    break


                # # 5. 发布ROS话题（新增功能）
                # if boxes is not None:
                #     # 5.2 发布带标注的图像
                #     self.publish_annotated_image(frame, boxes, classes, scores, ros_image.header)
                    
                #     # 5.4 发布边界框信息
                #     self.publish_bounding_boxes(boxes, classes, scores, ros_image.header)
            
                # # 6. 显示结果（原有功能保留）
                # img_1 = cv2.cvtColor(self.rknn_lite.img_rgb, cv2.COLOR_RGB2BGR)
                # if boxes is not None:
                #     # 注意：这里draw函数使用的是640x640坐标
                #     # 我们临时缩放回去进行绘制
                #     temp_boxes = boxes.copy()
                #     temp_boxes[:, [0, 2]] /= scale_x
                #     temp_boxes[:, [1, 3]] /= scale_y
                #     draw(img_1, temp_boxes, scores, classes)
                
                cv2.imshow("YOLOv5 Detection", img_1)
                cv2.waitKey(1)
            
        except Exception as e:
            pass
 #           rospy.logerr(f"处理错误: {e}")
        finally:
            # 清理资源
            cap.release()
            cv2.destroyAllWindows()
            self.rknn_lite.release()
  #          rospy.loginfo("摄像头已关闭，资源已释放")
    
if __name__ == '__main__':
    try:
        ensure_logging_integrity()
        ros_interface = ROSInterface()
        ros_interface.run()
        
    except KeyboardInterrupt:
        pass

    except Exception as e:
       # pass
        rospy.logerr("wrong")
        rospy.logerr(f"程序错误: {e}")
    finally:
        cv2.destroyAllWindows()
    #    rospy.loginfo("程序结束")
    # def publish_annotated_image(self, cv_image, boxes, classes, scores, header):
    #     """
    #     发布带标注的图像
    #     """
    #     annotated = cv_image.copy()
        
    #     for box, cls, score in zip(boxes, classes, scores):
    #         x1, y1, x2, y2 = map(int, box)
            
    #         # 绘制边界框
    #         cv2.rectangle(annotated, (x1, y1), (x2, y2), (0, 255, 0), 2)
            
    #         # 绘制标签
    #         label = f"{CLASSES[int(cls)]}: {score:.2f}"
    #         cv2.putText(annotated, label, (x1, y1-10), 
    #                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)
        
    #     # 添加标题
    #     cv2.putText(annotated, "YOLOv5 Detection", (10, 30), 
    #                cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
        
    #     # 转换为ROS消息并发布
    #     try:
    #         ros_image = self.bridge.cv2_to_imgmsg(annotated, "bgr8")
    #         ros_image.header = header
    #         self.annotated_pub.publish(ros_image)
    #     except Exception as e:
    #         rospy.logerr(f"发布标注图像失败: {e}")
    
    # def publish_detection_results(self, boxes, classes, scores, header):
    #     """
    #     发布检测结果（JSON格式）
    #     使用sensor_msgs/Image消息来传输文本数据
    #     """
    #     detections = []
        
    #     for box, cls, score in zip(boxes, classes, scores):
    #         detection = {
    #             "class": CLASSES[int(cls)],
    #             "class_id": int(cls),
    #             "score": float(score),
    #             "bbox": {
    #                 "x1": float(box[0]),
    #                 "y1": float(box[1]),
    #                 "x2": float(box[2]),
    #                 "y2": float(box[3]),
    #                 "width": float(box[2] - box[0]),
    #                 "height": float(box[3] - box[1])
    #             }
    #         }
    #         detections.append(detection)
        
    #     # 创建结果字典
    #     result = {
    #         "timestamp": header.stamp.to_sec(),
    #         "frame_id": header.frame_id,
    #         "detections": detections,
    #         "detection_count": len(detections)
    #     }
        
    #     # 转换为JSON字符串
    #     result_json = json.dumps(result, indent=2)
        
    #     # 发布为文本消息（使用Image消息的data字段）
    #     try:
    #         # 创建一个空的图像消息来携带文本数据
    #         text_msg = Image()
    #         text_msg.header = header
    #         text_msg.height = 1
    #         text_msg.width = len(result_json)
    #         text_msg.encoding = "mono8"
    #         text_msg.step = len(result_json)
    #         text_msg.data = result_json.encode('utf-8')
    #         self.result_pub.publish(text_msg)
    #     except Exception as e:
    #         rospy.logerr(f"发布检测结果失败: {e}")
    
    # def publish_bounding_boxes(self, boxes, classes, scores, header):
    #     """
    #     发布边界框可视化图像（用于调试）
    #     """
    #     # 创建边界框可视化图像
    #     bbox_img = np.zeros((480, 640, 3), dtype=np.uint8)
        
    #     for box, cls, score in zip(boxes, classes, scores):
    #         x1, y1, x2, y2 = map(int, box)
            
    #         # 归一化到可视化图像尺寸
    #         vis_x1 = int(x1 * 640 / 1920)  # 假设原始图像1920x1080
    #         vis_y1 = int(y1 * 480 / 1080)
    #         vis_x2 = int(x2 * 640 / 1920)
    #         vis_y2 = int(y2 * 480 / 1080)
            
    #         # 绘制边界框
    #         cv2.rectangle(bbox_img, (vis_x1, vis_y1), (vis_x2, vis_y2), (0, 255, 0), 1)
            
    #         # 简单文本
    #         cv2.putText(bbox_img, f"C{int(cls)}", (vis_x1, vis_y1-5), 
    #                    cv2.FONT_HERSHEY_SIMPLEX, 0.3, (0, 255, 0), 1)
        
    #     # 发布
    #     try:
    #         bbox_msg = self.bridge.cv2_to_imgmsg(bbox_img, "bgr8")
    #         bbox_msg.header = header
    #         self.bbox_pub.publish(bbox_msg)
    #     except Exception as e:
    #         rospy.logerr(f"发布边界框图像失败: {e}")
