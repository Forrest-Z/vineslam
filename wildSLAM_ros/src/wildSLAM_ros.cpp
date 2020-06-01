#include "../include/wildSLAM_ros.hpp"

namespace wildSLAM
{

void wildSLAM_ros::odomListener(const nav_msgs::OdometryConstPtr& msg)
{
  // Convert odometry msg to pose msg
  tf::Pose            pose;
  geometry_msgs::Pose odom_pose = (*msg).pose.pose;
  tf::poseMsgToTF(odom_pose, pose);

  // Check if yaw is NaN
  float yaw = static_cast<float>(tf::getYaw(pose.getRotation()));
  if (yaw != yaw)
    yaw = 0;

  // If it is the first iteration - initialize the Pose
  // relative to the previous frame
  if (init) {
    p_odom.x   = (*msg).pose.pose.position.x;
    p_odom.y   = (*msg).pose.pose.position.y;
    p_odom.yaw = yaw;
    odom = wildSLAM::pose(0., 0., 0., 0., 0., 0.);
    return;
  }

  // Integrate odometry pose to convert to the map frame
  odom.x += static_cast<float>(msg->pose.pose.position.x) - p_odom.x;
  odom.y += static_cast<float>(msg->pose.pose.position.y) - p_odom.y;
  odom.z     = 0;
  odom.roll  = 0;
  odom.pitch = 0;
  odom.yaw   += (yaw - p_odom.yaw);

  // Save current odometry pose to use in the next iteration
  p_odom.x   = msg->pose.pose.position.x;
  p_odom.y   = msg->pose.pose.position.y;
  p_odom.yaw = yaw;
}

void wildSLAM_ros::callbackFct(const sensor_msgs::ImageConstPtr& left_image,
                               const sensor_msgs::ImageConstPtr& depth_image,
                               const vision_msgs::Detection2DArrayConstPtr& dets)
{
  // Declaration of the arrays that will constitute the SLAM observations
  std::vector<int>   labels;
  std::vector<float> bearings;
  std::vector<float> depths;

  // Loop over all the bounding box detections
  for (const auto& detection : (*dets).detections) {
    // Load a single bounding box detection
    vision_msgs::BoundingBox2D m_bbox = detection.bbox;

    // Calculate the bearing and depth of the detected object
    float depth;
    float bearing;
    computeObsv(*depth_image,
                static_cast<int>(m_bbox.center.x - m_bbox.size_x / 2),
                static_cast<int>(m_bbox.center.y - m_bbox.size_y / 2),
                static_cast<int>(m_bbox.center.x + m_bbox.size_x / 2),
                static_cast<int>(m_bbox.center.y + m_bbox.size_y / 2),
                depth,
                bearing);

    // Check if the calculated depth is valid
    if (depth == -1)
      continue;

    // Insert the measures in the observations arrays
    labels.push_back(detection.results[0].id);
    depths.push_back(depth);
    bearings.push_back(bearing);
  }

  // - Data needed to compute the maps
  cv::Mat img =
      cv_bridge::toCvShare(left_image, sensor_msgs::image_encodings::BGR8)->image;
  auto* raw_depths = (float*)(&(*depth_image).data[0]);

  std::vector<Feature> m_features;
  pose robot_pose;

  if (init && bearings.size() > 1) {
    // Initialize the localizer and get first particles distribution
    localizer->init(pose(0, 0, 0, 0, 0, 0.));
    robot_pose = localizer->getPose();

    // Initialize the mapper2D
    mapper2D->init(robot_pose, bearings, depths, labels, *grid_map);

    // Initialize the mapper3D
    mapper3D->localMap(img, raw_depths, m_features);
    mapper3D->globalMap(m_features, robot_pose, *grid_map);

    init = false;
  } else if (!init) {

    // --------- Build local maps to use on localization
    // - Compute 2D local map of landmarks on camera's referential frame
    std::vector<Landmark> m_landmarks;
    mapper2D->localMap(bearings, depths, m_landmarks);
    // - Compute 3D local map of features on camera's referential frame
    mapper3D->localMap(img, raw_depths, m_features);

    // ------- LOCALIZATION PROCEDURE ---------- //
    localizer->process(odom, m_landmarks, m_features, *grid_map);
    robot_pose = localizer->getPose();

    // ------- MULTI-LAYER MAPPING ------------ //
    // ---------------------------------------- //
    // Compute 3D map using estimated robot pose
    mapper3D->globalMap(m_features, robot_pose, *grid_map);
    // ---------------------------------------- //
    // Execute the 2D map estimation
    mapper2D->process(robot_pose, bearings, depths, labels, *grid_map);
    // ---------------------------------------- //

    // Convert robot pose to tf::Transform corresponding
    // to the camera to map transformation
    tf::Quaternion q;
    q.setRPY(robot_pose.roll, robot_pose.pitch, robot_pose.yaw);
    q.normalize();
    tf::Transform cam2map;
    cam2map.setRotation(q);
    cam2map.setOrigin(tf::Vector3(robot_pose.x, robot_pose.y, robot_pose.z));

    // Convert wildSLAM pose to ROS pose and publish it
    geometry_msgs::PoseStamped pose_stamped;
    pose_stamped.header             = depth_image->header;
    pose_stamped.header.frame_id    = "map";
    pose_stamped.pose.position.x    = robot_pose.x;
    pose_stamped.pose.position.y    = robot_pose.y;
    pose_stamped.pose.position.z    = robot_pose.z;
    pose_stamped.pose.orientation.x = q.x();
    pose_stamped.pose.orientation.y = q.y();
    pose_stamped.pose.orientation.z = q.z();
    pose_stamped.pose.orientation.w = q.w();
    pose_publisher.publish(pose_stamped);

    // Push back the current pose to the path container and publish it
    path.push_back(pose_stamped);
    nav_msgs::Path ros_path;
    ros_path.header          = depth_image->header;
    ros_path.header.frame_id = "map";
    ros_path.poses           = path;
    path_publisher.publish(ros_path);

    // Publish cam-to-map tf::Transform
    static tf::TransformBroadcaster br;
    br.sendTransform(
        tf::StampedTransform(cam2map, pose_stamped.header.stamp, "map", "cam"));

    // ---------- Publish Multi-layer map ------------- //
    // Publish the grid map
    publishGridMap(depth_image->header);
    // Publish the 2D map
    publish2DMap(depth_image->header, robot_pose, bearings, depths);
    // Publish 3D point map
    publish3DMap();
    // ------------------------------------------------ //

#ifdef DEBUG
    // Publish all poses for DEBUG
    // ----------------------------------------------------------------------------
    std::vector<pose> poses;
    (*localizer).getParticles(poses);
    geometry_msgs::PoseArray ros_poses;
    ros_poses.header          = depth_image->header;
    ros_poses.header.frame_id = "map";
    for (const auto& pose : poses) {
      tf::Quaternion q;
      q.setRPY(pose.roll, pose.pitch, pose.yaw);
      q.normalize();

      geometry_msgs::Pose m_pose;
      m_pose.position.x    = pose.x;
      m_pose.position.y    = pose.y;
      m_pose.position.z    = pose.z;
      m_pose.orientation.x = q.x();
      m_pose.orientation.y = q.y();
      m_pose.orientation.z = q.z();
      m_pose.orientation.w = q.w();

      ros_poses.poses.push_back(m_pose);
    }
    poses_publisher.publish(ros_poses);
    // ----------------------------------------------------------------------------
    // Publish debug 3D maps - source and aligned ICP maps
    // ----------------------------------------------------------------------------
    // - Publish source map
    publish3DMap(m_features, source_map_publisher);
    // - Compute and publish aligned map
    std::array<float, 9> Rot = {0., 0., 0., 0., 0., 0., 0., 0., 0.};
    robot_pose.toRotMatrix(Rot);
    std::array<float, 3> trans = {robot_pose.x, robot_pose.y, robot_pose.z};
    std::vector<Feature> aligned;
    for (const auto& feature : m_features) {
      // - Convert them to map's referential using the robot pose
      Feature m_feature = feature;

      point m_pt;
      m_pt.x = feature.pos.x * Rot[0] + feature.pos.y * Rot[1] +
               feature.pos.z * Rot[2] + trans[0];
      m_pt.y = feature.pos.x * Rot[3] + feature.pos.y * Rot[4] +
               feature.pos.z * Rot[5] + trans[1];
      m_pt.z = feature.pos.x * Rot[6] + feature.pos.y * Rot[7] +
               feature.pos.z * Rot[8] + trans[2];

      m_feature.pos = m_pt;
      aligned.push_back(m_feature);
    }
    // - Publish the aligned map
    publish3DMap(aligned, aligned_map_publisher);
    // ----------------------------------------------------------------------------
#endif
  }
}

void wildSLAM_ros::computeObsv(const sensor_msgs::Image& depth_img,
                               const int&                xmin,
                               const int&                ymin,
                               const int&                xmax,
                               const int&                ymax,
                               float&                    depth,
                               float&                    bearing) const
{
  // Declare array with all the disparities computed
  auto* depths = (float*)(&(depth_img).data[0]);

  // Set minimum and maximum depth values to consider
  float range_min = 0.01;
  float range_max = 10.0;

  std::map<float, float> dtheta;
  for (int i = xmin; i < xmax; i++) {
    for (int j = ymin; j < ymax; j++) {
      int idx = i + depth_img.width * j;

      // Fill the depth array with the values of interest
      if (std::isfinite(depths[idx]) && depths[idx] > range_min &&
          depths[idx] < range_max) {
        float x         = depths[idx];
        float y         = -(static_cast<float>(i) - cx) * (x / fx);
        float m_depth   = static_cast<float>(sqrt(pow(x, 2) + pow(y, 2)));
        dtheta[m_depth] = atan2(y, x);
      }
    }
  }

  // compute minimum of all observations
  size_t n_depths = dtheta.size();
  if (n_depths > 0) {
    depth   = dtheta.begin()->first;
    bearing = dtheta.begin()->second;
  } else {
    depth   = -1;
    bearing = -1;
  }
}

}; // namespace wildSLAM
