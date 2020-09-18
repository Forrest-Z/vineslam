#pragma once

#include <iostream>

namespace vineslam
{

struct Parameters {
  // -----------------------------------
  // ------ ROS topic names
  // -----------------------------------
  std::string bagfile_name;
  std::string odom_topic;
  std::string rs_odom_topic;
  std::string tf_topic;
  std::string fix_topic;
  std::string depth_img_topic;
  std::string left_img_topic;
  std::string detections_topic;
  std::string pcl_topic;

  // -----------------------------------
  // ------ System flags
  // -----------------------------------
  bool debug{};
  bool use_vegetation_lines{};
  bool use_landmarks{};
  bool use_ground_plane{};
  bool use_corners{};
  bool use_icp{};
  bool use_gps{};

  // -----------------------------------
  // ------ GPS datum
  // -----------------------------------
  float latitude{};
  float longitude{};

  // -----------------------------------
  // ------ Camera info parameters
  // -----------------------------------
  float baseline{};
  float depth_hfov{};
  float depth_vfov{};
  int   img_width{};
  int   img_height{};
  float fx{};
  float fy{};
  float cx{};
  float cy{};

  // -----------------------------------
  // ------ Multi-layer mapping parameters
  // -----------------------------------
  // - Grid map
  float       gridmap_origin_x{};
  float       gridmap_origin_y{};
  float       gridmap_width{};
  float       gridmap_height{};
  float       gridmap_resolution{};
  std::string gridmap_metric;
  bool        save_map{};
  std::string map_output_folder;
  std::string map_input_file;
  // - 3D map
  int   hessian_threshold{};
  float max_range{};
  float max_height{};
  int   downsample_factor{};
  int   icp_max_iters{};
  float icp_distance_threshold{};
  bool  icp_reject_outliers{};

  // -----------------------------------
  // ------ Particle filter parameters
  // -----------------------------------
  int   number_particles{};
  float srr{};
  float srt{};
  float str{};
  float stt{};
  float sigma_xy{};
  float sigma_z{};
  float sigma_roll{};
  float sigma_pitch{};
  float sigma_yaw{};
  float sigma_landmark_matching{};
  float sigma_feature_matching{};
  float sigma_corner_matching{};
  float sigma_vegetation_lines_yaw{};
  float sigma_ground_rp{};
  float sigma_gps{};
  int   number_clusters{};

  // -----------------------------------
  // ------ METHODS
  // -----------------------------------
  Parameters() = default;
};

} // namespace vineslam