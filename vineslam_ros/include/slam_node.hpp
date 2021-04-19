#pragma once

#include "vineslam_ros.hpp"

namespace vineslam
{
class SLAMNode : public VineSLAM_ros
{
public:
  // Class constructor that
  // - Initialize the ROS node
  // - Define the publish and subscribe topics
  SLAMNode();

  // Class destructor - saves the map to an output xml file
  ~SLAMNode();

private:
  // Parameters loader
  void loadParameters(Parameters& params);

  // ROS subscribers
  rclcpp::Subscription<vineslam_msgs::msg::FeatureArray>::SharedPtr feature_subscriber_;
  rclcpp::Subscription<vision_msgs::msg::Detection3DArray>::SharedPtr landmark_subscriber_;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr scan_subscriber_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_subscriber_;
  rclcpp::Subscription<geometry_msgs::msg::PoseWithCovarianceStamped>::SharedPtr gps_subscriber_;
  rclcpp::Subscription<geometry_msgs::msg::Vector3Stamped>::SharedPtr imu_subscriber_;

  // ROS services
  rclcpp::Service<vineslam_ros::srv::StartMapRegistration>::SharedPtr start_reg_srv_;
  rclcpp::Service<vineslam_ros::srv::StopMapRegistration>::SharedPtr stop_reg_srv_;
  rclcpp::Service<vineslam_ros::srv::SaveMap>::SharedPtr save_map_srv_;
};

}  // namespace vineslam