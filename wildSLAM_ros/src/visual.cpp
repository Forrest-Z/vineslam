#include "../include/wildSLAM_ros.hpp"

void wildSLAM_ros::SLAMNode::publishGridMap(const std_msgs::Header& header)
{
  // Define ROS occupancy grid map
  nav_msgs::OccupancyGrid occ_map;
  occ_map.header          = header;
  occ_map.header.frame_id = "map";

  // Set the map metadata
  nav_msgs::MapMetaData metadata;
  metadata.origin.position.x    = occ_origin.x;
  metadata.origin.position.y    = occ_origin.y;
  metadata.origin.position.z    = occ_origin.z;
  metadata.origin.orientation.x = 0.;
  metadata.origin.orientation.y = 0.;
  metadata.origin.orientation.z = 0.;
  metadata.origin.orientation.w = 1.;
  metadata.resolution           = occ_resolution;
  metadata.width                = (float)occ_width / occ_resolution;
  metadata.height               = (float)occ_height / occ_resolution;
  occ_map.info                  = metadata;

  // Fill the occupancy grid map
  occ_map.data.resize(metadata.width * metadata.height);
  // - compute x and y bounds
  int xmin = occ_origin.x / occ_resolution;
  int xmax = xmin + (float)occ_width / occ_resolution;
  int ymin = occ_origin.y / occ_resolution;
  int ymax = ymin + (float)occ_height / occ_resolution;
  for (int i = xmin; i < xmax; i++) {
    for (int j = ymin; j < ymax; j++) {
      int8_t number_landmarks = (*grid_map)(i, j).landmarks.size();

      int m_i = i - (occ_origin.x / occ_resolution);
      int m_j = j - (occ_origin.y / occ_resolution);
      int idx = m_i + m_j * ((float)occ_width / occ_resolution);

       occ_map.data[idx] = number_landmarks * 10;
    }
  }

  // Publish the map
  mapOcc_publisher.publish(occ_map);
}

void wildSLAM_ros::SLAMNode::publish2DMap(const std_msgs::Header&   header,
                                          const pose6D&             pose,
                                          const std::vector<float>& bearings,
                                          const std::vector<float>& depths)
{
  visualization_msgs::MarkerArray marker_array;
  visualization_msgs::Marker      marker;
  visualization_msgs::MarkerArray ellipse_array;
  visualization_msgs::Marker      ellipse;

  // Define marker layout
  marker.ns                 = "/markers";
  marker.type               = visualization_msgs::Marker::CYLINDER;
  marker.action             = visualization_msgs::Marker::ADD;
  marker.scale.x            = 0.1;
  marker.scale.y            = 0.1;
  marker.scale.z            = 0.3;
  marker.pose.orientation.x = 0.0;
  marker.pose.orientation.y = 0.0;
  marker.pose.orientation.z = 0.0;
  marker.pose.orientation.w = 1.0;
  marker.color.r            = 0.0f;
  marker.color.g            = 0.0f;
  marker.color.b            = 1.0f;
  marker.color.a            = 1.0;
  marker.lifetime           = ros::Duration();

  // Define marker layout
  ellipse.ns                 = "/ellipses";
  ellipse.type               = visualization_msgs::Marker::CYLINDER;
  ellipse.action             = visualization_msgs::Marker::ADD;
  ellipse.scale.z            = 0.01f;
  ellipse.pose.orientation.x = 0.0f;
  ellipse.pose.orientation.y = 0.0f;
  ellipse.color.r            = 0.0f;
  ellipse.color.g            = 1.0f;
  ellipse.color.b            = 0.0f;
  ellipse.color.a            = 1.0f;
  ellipse.lifetime           = ros::Duration();

  // Publish markers
  int id = 1;
  for (auto it = (*grid_map).begin(); it != (*grid_map).end(); it++) {
    for (size_t i = 0; i < it->landmarks.size(); i++) {
      // Draw landmark mean
      marker.id              = id;
      marker.header          = header;
      marker.header.frame_id = "map";
      marker.pose.position.x = it->landmarks[i].pos.x;
      marker.pose.position.y = it->landmarks[i].pos.y;
      marker.pose.position.z = 0;

      marker_array.markers.push_back(marker);

      // Draw landmark standard deviation
      tf2::Quaternion q;
      q.setRPY(0, 0, it->landmarks[i].stdev.TH);

      ellipse.id                 = id;
      ellipse.header             = header;
      ellipse.header.frame_id    = "map";
      ellipse.pose.position.x    = it->landmarks[i].pos.x;
      ellipse.pose.position.y    = it->landmarks[i].pos.y;
      ellipse.pose.position.z    = 0;
      ellipse.scale.x            = 3 * it->landmarks[i].stdev.stdX;
      ellipse.scale.y            = 3 * it->landmarks[i].stdev.stdY;
      ellipse.pose.orientation.x = q.x();
      ellipse.pose.orientation.y = q.y();
      ellipse.pose.orientation.z = q.z();
      ellipse.pose.orientation.w = q.w();

      ellipse_array.markers.push_back(ellipse);

      id++;
    }
  }

  // Draw ellipse that characterizes particles distribution
  tf2::Quaternion q;
  q.setRPY(0, 0, pose.dist.TH);

  ellipse.id                 = map2D.size() + 1;
  ellipse.header             = header;
  ellipse.header.frame_id    = "map";
  ellipse.pose.position.x    = pose.x;
  ellipse.pose.position.y    = pose.y;
  ellipse.pose.position.z    = 0;
  ellipse.scale.x            = 3 * pose.dist.stdX;
  ellipse.scale.y            = 3 * pose.dist.stdY;
  ellipse.pose.orientation.x = q.x();
  ellipse.pose.orientation.y = q.y();
  ellipse.pose.orientation.z = q.z();
  ellipse.pose.orientation.w = q.w();
  ellipse.color.r            = 0.0f;
  ellipse.color.g            = 0.0f;
  ellipse.color.b            = 1.0f;
  ellipse.color.a            = 1.0f;
  ellipse_array.markers.push_back(ellipse);

  map2D_publisher.publish(marker_array);
  map2D_publisher.publish(ellipse_array);
}

void wildSLAM_ros::SLAMNode::publish3DMap(const std_msgs::Header& header)
{
  // Declare the octree to map (trunk map or feature map)
  OcTreeT* octree = feature_octree;

  visualization_msgs::MarkerArray octomapviz;
  // each array stores all cubes of a different size, one for each depth level:
  octomapviz.markers.resize((*octree).getTreeDepth() + 1);

  for (OcTreeT::iterator it  = (*octree).begin((*octree).getTreeDepth()),
                         end = (*octree).end();
       it != end;
       ++it) {

    if ((*it).isColorSet()) {
      // if ((*octree).isNodeOccupied(*it)) {
      float size = it.getSize();
      float x    = it.getX();
      float y    = it.getY();
      float z    = it.getZ();

      std_msgs::ColorRGBA _color;
      _color.r = (*it).getColor().r / 255.;
      _color.g = (*it).getColor().g / 255.;
      _color.b = (*it).getColor().b / 255.;
      _color.a = 1.0;

      unsigned idx = it.getDepth();
      assert(idx < octomapviz.markers.size());

      geometry_msgs::Point cubeCenter;
      cubeCenter.x = x;
      cubeCenter.y = y;
      cubeCenter.z = z;

      octomapviz.markers[idx].points.push_back(cubeCenter);
      octomapviz.markers[idx].colors.push_back(_color);
    }
  }

  for (unsigned i = 0; i < octomapviz.markers.size(); ++i) {
    float size = (*octree).getNodeSize(i);

    octomapviz.markers[i].header.frame_id = "map";
    octomapviz.markers[i].header.stamp    = header.stamp;
    octomapviz.markers[i].ns              = "map";
    octomapviz.markers[i].id              = i;
    octomapviz.markers[i].type            = visualization_msgs::Marker::CUBE_LIST;
    octomapviz.markers[i].scale.x         = size;
    octomapviz.markers[i].scale.y         = size;
    octomapviz.markers[i].scale.z         = size;

    if (octomapviz.markers[i].points.size() > 0)
      octomapviz.markers[i].action = visualization_msgs::Marker::ADD;
    else
      octomapviz.markers[i].action = visualization_msgs::Marker::DELETE;
  }

  map3D_publisher.publish(octomapviz);
}
