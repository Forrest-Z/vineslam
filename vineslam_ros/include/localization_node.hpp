#pragma once

// vineslam members
#include <feature.hpp>
#include <localizer.hpp>
#include <occupancy_map.hpp>
#include <mapper2D.hpp>
#include <mapper3D.hpp>
#include <math/point.hpp>
#include <math/pose.hpp>
#include <math/const.hpp>
#include <mapXML/map_writer.hpp>
#include <mapXML/map_parser.hpp>

// std
#include <iostream>

// ROS
#include <cv_bridge/cv_bridge.h>
#include <geometry_msgs/PoseArray.h>
#include <image_transport/image_transport.h>
#include <message_filters/subscriber.h>
#include <message_filters/time_synchronizer.h>
#include <nav_msgs/Odometry.h>
#include <nav_msgs/Path.h>
#include <ros/ros.h>
#include <sensor_msgs/NavSatFix.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/point_cloud_conversion.h>
#include <tf/transform_broadcaster.h>
#include <vision_msgs/Detection2D.h>
#include <vision_msgs/Detection2DArray.h>
#include <visualization_msgs/MarkerArray.h>
#include <nav_msgs/OccupancyGrid.h>
#include <pcl_ros/point_cloud.h>
#include <yaml-cpp/yaml.h>

// Services
#include <agrob_map_transform/GetPose.h>
#include <agrob_map_transform/SetDatum.h>

#define DEBUG 1

namespace vineslam
{
class LocalizationNode
{
public:
  // Class constructor that
  // - Initializes the ROS node
  // - Defines the publish and subscribe topics
  LocalizationNode(int argc, char** argv);

  // Class destructor - saves the map to an output xml file
  ~LocalizationNode();

  // Callback function that subscribes a rgb image, a  disparity image,
  // and the bounding boxes that locate the objects on the image
  void callbackFct(const sensor_msgs::ImageConstPtr&            left_image,
                   const sensor_msgs::ImageConstPtr&            depth_image,
                   const vision_msgs::Detection2DArrayConstPtr& dets);
  // Odometry callback function
  void odomListener(const nav_msgs::OdometryConstPtr& msg);
  // GPS callback function
  void gpsListener(const sensor_msgs::NavSatFixConstPtr& msg);

private:
  // Publish 2D semantic features map
  void publish2DMap(const std_msgs::Header& header);
  // Publish the 3D maps
  void publish3DMap();
  // Publish the 3D PCL planes map
  static void publish3DMap(const Plane& plane, const ros::Publisher& pub);
  // Publish a 3D PCL corners map
  static void publish3DMap(const std::vector<Corner>& corners,
                           const ros::Publisher&      pub);
  // Publish the grid map that contains all the maps
  void publishGridMap(const std_msgs::Header& header);

  // Computes the bearing depth of an object using the ZED disparity image
  // - Uses the point with minimum depth inside the bounding box
  void computeObsv(const sensor_msgs::Image& depth_img,
                   const int&                xmin,
                   const int&                ymin,
                   const int&                xmax,
                   const int&                ymax,
                   float&                    depth,
                   float&                    bearing) const;

  // GNSS heading estimator
  bool getGNSSHeading(const pose& gps_odom, const std_msgs::Header& header);

  // ROS publishers/services
  ros::Publisher     mapOCC_publisher;
  ros::Publisher     map2D_publisher;
  ros::Publisher     map3D_features_publisher;
  ros::Publisher     map3D_corners_publisher;
  ros::Publisher     map3D_planes_publisher;
  ros::Publisher     map3D_debug_publisher;
  ros::Publisher     pose_publisher;
  ros::Publisher     odom_publisher;
  ros::Publisher     path_publisher;
  ros::Publisher     poses_publisher;
  ros::Publisher     gps_publisher;
  ros::Publisher     normal_pub;
  ros::ServiceClient polar2pose;
  ros::ServiceClient set_datum;

  // Classes object members
  Localizer*    localizer;
  OccupancyMap* grid_map;
  Mapper2D*     mapper2D;
  Mapper3D*     mapper3D;

  // Array of poses to store and publish the robot path
  std::vector<geometry_msgs::PoseStamped> path;

  // Motion variables
  pose odom;
  pose p_odom;
  pose robot_pose;
  pose gps_pose;

  // GNSS variables
  int     datum_autocorrection_stage;
  int32_t global_counter;
  float   datum_orientation[360][4]{};
  bool    has_converged{};

  // Input parameters
  // ------------------------
  std::string config_path;
  // Camera info parameters
  int   img_width;
  int   img_height;
  float cam_height;
  float fx;
  float fy;
  float cx;
  float cy;
  // ------------------------
  // Grid map dimensions
  // NOTE: corners are in reference to the given origin
  point occ_origin;
  float occ_resolution;
  float occ_width;
  float occ_height;
  //------------------------
  // System settings
  bool  use_gps;
  float gps_init_lat;
  float gps_init_long;
  float gps_init_head;

  // Initialize flag
  bool init;
};

}; // namespace vineslam
