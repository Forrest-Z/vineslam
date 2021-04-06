#pragma once

// vineslam members
#include <vineslam/feature/semantic.hpp>
#include <vineslam/feature/visual.hpp>
#include <vineslam/feature/three_dimensional.hpp>
#include <vineslam/localization/localizer.hpp>
#include <vineslam/mapping/occupancy_map.hpp>
#include <vineslam/mapping/elevation_map.hpp>
#include <vineslam/mapping/landmark_mapping.hpp>
#include <vineslam/mapping/visual_mapping.hpp>
#include <vineslam/mapping/lidar_mapping.hpp>
#include <vineslam/math/Point.hpp>
#include <vineslam/math/Pose.hpp>
#include <vineslam/math/const.hpp>
#include <vineslam/mapxml/map_writer.hpp>
#include <vineslam/mapxml/map_parser.hpp>
#include <vineslam/utils/save_data.hpp>
#include <vineslam/utils/Timer.hpp>
// ----------------------------
#include <vineslam_msgs/msg/particle.hpp>
#include <vineslam_msgs/msg/report.hpp>
#include <vineslam_msgs/msg/feature.hpp>
#include <vineslam_msgs/msg/feature_array.hpp>
// ----------------------------
#include <vineslam_ros/srv/start_map_registration.hpp>
#include <vineslam_ros/srv/stop_map_registration.hpp>
#include <vineslam_ros/srv/save_map.hpp>
// ----------------------------

// std
#include <iostream>
#include <chrono>
#include <ctime>

// ROS
#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/pose_array.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <nav_msgs/msg/path.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <tf2_ros/transform_listener.h>
#include <tf2_ros/transform_broadcaster.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <tf2/utils.h>
#include <vision_msgs/msg/detection3_d.hpp>
#include <vision_msgs/msg/detection3_d_array.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/filters/filter.h>

namespace vineslam
{
class VineSLAM_ros : public rclcpp::Node
{
public:
  VineSLAM_ros() = default;
  VineSLAM_ros(const std::string& node) : Node(node)
  {
  }

  // Runtime execution routines
  virtual void init();

  virtual void loop();

  virtual void loopOnce();

  virtual void process();

  // Stereo camera images callback function
  void imageFeatureListener(const vineslam_msgs::msg::FeatureArray::SharedPtr features);

  // Landmark detection callback function
  void landmarkListener(const vision_msgs::msg::Detection3DArray::SharedPtr dets);

  // Scan callback function
  void scanListener(const sensor_msgs::msg::PointCloud2::SharedPtr msg);

  // Odometry callback function
  void odomListener(const nav_msgs::msg::Odometry::SharedPtr msg);

  // GPS callback function
  void gpsListener(const geometry_msgs::msg::PoseWithCovarianceStamped::SharedPtr msg);

  // IMU callback function
  void imuListener(const geometry_msgs::msg::Vector3Stamped::SharedPtr msg);

  // Services callbacks
  bool startRegistration(vineslam_ros::srv::StartMapRegistration::Request::SharedPtr,
                         vineslam_ros::srv::StartMapRegistration::Response::SharedPtr);
  bool stopRegistration(vineslam_ros::srv::StopMapRegistration::Request::SharedPtr,
                        vineslam_ros::srv::StopMapRegistration::Response::SharedPtr);
  bool saveMap(vineslam_ros::srv::SaveMap::Request::SharedPtr, vineslam_ros::srv::SaveMap::Response::SharedPtr);

  // Conversions
  static void pose2TransformStamped(const tf2::Quaternion& q, const tf2::Vector3& t,
                                    geometry_msgs::msg::TransformStamped& tf);

  // ROS node
  rclcpp::Node::SharedPtr nh_;

  // Most recent message header received
  std_msgs::msg::Header header_;

  // Tf2 broadcaster
  std::shared_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster_;

  // Global thread to publish maps and other info
  void publishDenseInfo();
  // Publish 2D semantic features map
  void publish2DMap(const Pose& pose, const std::vector<float>& bearings, const std::vector<float>& depths) const;
  // Publish the elevation map
  void publishElevationMap() const;
  // Publish the 3D maps
  void publish3DMap();
  // Publish the 3D PCL planes
  void publish3DMap(const std::vector<Plane>& planes,
                    rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr pub);
  void publish3DMap(const Pose& r_pose, const std::vector<Plane>& planes,
                           rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr pub);
  // Publish the 3D PCL semi planes
  void publish3DMap(const std::vector<SemiPlane>& planes,
                    rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr pub);
  void publish3DMap(const Pose& r_pose, const std::vector<SemiPlane>& planes,
                           rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr pub);
  // Publish a 3D PCL corners map
  void publish3DMap(const std::vector<Corner>& corners,
                    const rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr pub);
  void publish3DMap(const Pose& r_pose, const std::vector<Corner>& corners,
                           rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr pub);
  // Publish a 3D PCL planar features map
  void publish3DMap(const std::vector<Planar>& planars,
                    const rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr pub);
  void publish3DMap(const Pose& r_pose, const std::vector<Planar>& planars,
                           rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr pub);
  // Publishes a box containing the grid map
  void publishGridMapLimits() const;
  // Publishes a VineSLAM state report for debug purposes
  void publishReport() const;

  // VineSLAM input data
  struct InputData
  {
    // Landmark labels array
    std::vector<int> land_labels_;
    // Landmark bearings array
    std::vector<float> land_bearings_;
    // Landmark depths array
    std::vector<float> land_depths_;
    // Image features
    std::vector<ImageFeature> image_features_;
    // Wheel odometry pose
    Pose wheel_odom_pose_;
    // Previous wheel odometry pose
    Pose p_wheel_odom_pose_;
    // GNSS pose
    Pose gnss_pose_;
    // IMU pose
    Pose imu_pose_;

    // LiDAR scan points
    std::vector<Point> scan_pts_;

    // Observation flags
    bool received_landmarks_;
    bool received_image_features_;
    bool received_odometry_;
    bool received_gnss_;
    bool received_scans_;
  } input_data_;

  // ROS publishers/services
  rclcpp::Publisher<vineslam_msgs::msg::Report>::SharedPtr vineslam_report_publisher_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr grid_map_publisher_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr elevation_map_publisher_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr map2D_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr map3D_features_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr map3D_corners_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr map3D_planars_publisher_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr map3D_planes_publisher_;
  rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr pose_publisher_;
  rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr path_publisher_;
  rclcpp::Publisher<geometry_msgs::msg::PoseArray>::SharedPtr poses_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr corners_local_publisher_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr planars_local_publisher_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr planes_local_publisher_;
  rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr gps_pose_publisher_;

  // Classes object members
  Parameters params_;
  Localizer* localizer_;
  ElevationMap* elevation_map_;
  OccupancyMap* grid_map_;
  LandmarkMapper* land_mapper_;
  VisualMapper* vis_mapper_;
  LidarMapper* lid_mapper_;
  Timer* timer_;
  Observation obsv_;

  // Array of poses to store and publish the robot path
  std::vector<geometry_msgs::msg::PoseStamped> path_;

  // Motion variables
  Pose init_odom_pose_;
  Pose init_gps_pose_;
  Pose robot_pose_;

  // odom -> satellite pose variables
  geometry_msgs::msg::TransformStamped satellite2base_msg_;
  float rtk_z_offset_;

  // Initialization flags
  bool init_flag_;
  bool init_gps_;
  bool init_odom_;
  bool register_map_;
};

}  // namespace vineslam
