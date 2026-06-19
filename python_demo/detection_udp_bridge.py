#!/usr/bin/env python3
import json
import socket

import rospy
from std_msgs.msg import String


def main():
    rospy.init_node("detection_udp_bridge")

    listen_ip = rospy.get_param("~listen_ip", "0.0.0.0")
    listen_port = int(rospy.get_param("~listen_port", 8890))
    topic = rospy.get_param("~topic", "/vision/detections")
    buffer_size = int(rospy.get_param("~buffer_size", 65535))

    pub = rospy.Publisher(topic, String, queue_size=10)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((listen_ip, listen_port))
    sock.settimeout(0.5)

    rospy.loginfo("Detection UDP bridge listening on %s:%d -> %s", listen_ip, listen_port, topic)

    while not rospy.is_shutdown():
        try:
            data, addr = sock.recvfrom(buffer_size)
        except socket.timeout:
            continue
        except OSError as exc:
            rospy.logerr("UDP receive failed: %s", exc)
            break

        try:
            text = data.decode("utf-8")
            json.loads(text)
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            rospy.logwarn_throttle(2.0, "Invalid detection packet from %s:%d: %s", addr[0], addr[1], exc)
            continue

        pub.publish(String(data=text))

    sock.close()


if __name__ == "__main__":
    main()
