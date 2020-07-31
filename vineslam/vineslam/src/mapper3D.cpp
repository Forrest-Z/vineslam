#include "mapper3D.hpp"

namespace vineslam
{

Mapper3D::Mapper3D(const std::string& config_path)
{
  // Load configuration file
  YAML::Node config = YAML::LoadFile(config_path);

  // Load camera info parameters
  img_width       = config["camera_info"]["img_width"].as<int>();
  img_height      = config["camera_info"]["img_height"].as<int>();
  fx              = config["camera_info"]["fx"].as<float>();
  fy              = config["camera_info"]["fy"].as<float>();
  cx              = config["camera_info"]["cx"].as<float>();
  cy              = config["camera_info"]["cy"].as<float>();
  auto depth_hfov = config["camera_info"]["depth_hfov"].as<float>() * DEGREE_TO_RAD;
  auto depth_vfov = config["camera_info"]["depth_vfov"].as<float>() * DEGREE_TO_RAD;
  // Load 3D map parameters
  metric     = config["multilayer_mapping"]["grid_map"]["metric"].as<std::string>();
  max_range  = config["multilayer_mapping"]["map_3D"]["max_range"].as<float>();
  max_height = config["multilayer_mapping"]["map_3D"]["max_height"].as<float>();
  // Feature detector
  hessian_threshold =
      config["multilayer_mapping"]["image_feature"]["hessian_threshold"].as<int>();

  // Set pointcloud feature parameters
  max_iters      = 20;
  dist_threshold = 0.08;
  downsample_f =
      config["multilayer_mapping"]["cloud_feature"]["downsample_factor"].as<int>();

  // Threshold to consider correspondences
  correspondence_threshold = 0.02;
}

// -------------------------------------------------------------------------------
// ---- 3D image feature map functions
// -------------------------------------------------------------------------------

void Mapper3D::localSurfMap(const cv::Mat&             img,
                            const float*               depths,
                            std::vector<ImageFeature>& out_features)
{
  // --------- Image feature extraction
  std::vector<ImageFeature> features;
  extractSurfFeatures(img, features);
  // ----------------------------------

  // --------- Build local map of 3D points ----------------------------------------
  for (const auto& feature : features) {
    int   idx     = feature.u + img.cols * feature.v;
    float m_depth = depths[idx];

    // Check validity of depth information
    if (!std::isfinite(depths[idx])) {
      continue;
    }

    point out_pt;
    point in_pt(static_cast<float>(feature.u), static_cast<float>(feature.v), 0.);
    pixel2base(in_pt, m_depth, out_pt);
    // Get the RGB pixel values
    auto* p = img.ptr<cv::Point3_<uchar>>(feature.v, feature.u);
    //------------------------------------------------------------------------------
    std::array<uint8_t, 3> c_int = {(*p).z, (*p).y, (*p).x};
    //------------------------------------------------------------------------------
    // Compute feature and insert on grid map
    float dist = std::sqrt((out_pt.x * out_pt.x) + (out_pt.y * out_pt.y) +
                           (out_pt.z * out_pt.z));
    if (out_pt.z < max_height && dist < max_range) {
      ImageFeature m_feature = feature;
      m_feature.r            = c_int[0];
      m_feature.g            = c_int[1];
      m_feature.b            = c_int[2];
      m_feature.pos          = out_pt;
      out_features.push_back(m_feature);
    }
  }
  // -------------------------------------------------------------------------------
}

void Mapper3D::globalSurfMap(const std::vector<ImageFeature>& features,
                             const pose&                      robot_pose,
                             OccupancyMap&                    grid_map) const
{
  // ------ Convert robot pose into homogeneous transformation
  std::array<float, 9> Rot{};
  robot_pose.toRotMatrix(Rot);
  std::array<float, 3> trans = {robot_pose.x, robot_pose.y, robot_pose.z};

  // ------ Insert features into the grid map
  for (const auto& image_feature : features) {
    // - First convert them to map's referential using the robot pose
    ImageFeature m_feature = image_feature;

    point m_pt;
    m_pt.x = image_feature.pos.x * Rot[0] + image_feature.pos.y * Rot[1] +
             image_feature.pos.z * Rot[2] + trans[0];
    m_pt.y = image_feature.pos.x * Rot[3] + image_feature.pos.y * Rot[4] +
             image_feature.pos.z * Rot[5] + trans[1];
    m_pt.z = image_feature.pos.x * Rot[6] + image_feature.pos.y * Rot[7] +
             image_feature.pos.z * Rot[8] + trans[2];

    m_feature.pos = m_pt;

    // - Then, look for correspondences in the local map
    ImageFeature correspondence{};
    float        best_correspondence = correspondence_threshold;
    bool         found               = false;
    for (const auto& m_image_feature : grid_map(m_pt.x, m_pt.y).surf_features) {
      float dist_min = m_pt.distance(m_image_feature.pos);

      if (dist_min < best_correspondence) {
        correspondence      = m_image_feature;
        best_correspondence = dist_min;
        found               = true;
      }
    }

    // Only search in the adjacent cells if we do not find in the source cell
    if (!found) {
      std::vector<Cell> adjacents;
      grid_map.getAdjacent(m_pt.x, m_pt.y, 2, adjacents);
      for (const auto& m_cell : adjacents) {
        for (const auto& m_image_feature : m_cell.surf_features) {
          float dist_min = m_pt.distance(m_image_feature.pos);
          if (dist_min < best_correspondence) {
            correspondence      = m_image_feature;
            best_correspondence = dist_min;
            found               = true;
          }
        }
      }
    }

    // - Then, insert the image feature into the grid map
    if (found) {
      point        new_pt = (m_pt + correspondence.pos) / 2.;
      ImageFeature new_image_feature(image_feature.u,
                                     image_feature.v,
                                     image_feature.r,
                                     image_feature.g,
                                     image_feature.b,
                                     new_pt);
      new_image_feature.laplacian = image_feature.laplacian;
      new_image_feature.signature = image_feature.signature;
      grid_map.update(correspondence, new_image_feature);
    } else {
      ImageFeature new_image_feature(image_feature.u,
                                     image_feature.v,
                                     image_feature.r,
                                     image_feature.g,
                                     image_feature.b,
                                     m_pt);
      new_image_feature.laplacian = image_feature.laplacian;
      new_image_feature.signature = image_feature.signature;
      grid_map.insert(new_image_feature);
    }
  }
}

void Mapper3D::extractSurfFeatures(const cv::Mat&             in,
                                   std::vector<ImageFeature>& out) const
{
  using namespace cv::xfeatures2d;

  // Array to store the features
  std::vector<cv::KeyPoint> kpts;
  // String to store the type of feature
  std::string type;
  // Matrix to store the descriptor
  cv::Mat desc;

  // Perform feature extraction
  auto surf = SURF::create(hessian_threshold);
  surf->detectAndCompute(in, cv::Mat(), kpts, desc);

  // Save features in the output array
  for (auto& kpt : kpts) {
    ImageFeature m_ft(kpt.pt.x, kpt.pt.y);
    m_ft.laplacian = kpt.class_id;
    out.push_back(m_ft);
  }

  // Save features descriptors
  for (int32_t i = 0; i < desc.rows; i++) {
    for (int32_t j = 0; j < desc.cols; j++) {
      out[i].signature.push_back(desc.at<float>(i, j));
    }
  }
}

// -------------------------------------------------------------------------------
// ---- 3D pointcloud feature map functions
// -------------------------------------------------------------------------------

void Mapper3D::reset()
{
  range_mat.resize(vertical_scans, horizontal_scans);
  ground_mat.resize(vertical_scans, horizontal_scans);
  label_mat.resize(vertical_scans, horizontal_scans);
  range_mat.fill(-1);
  ground_mat.setZero();
  label_mat.setZero();

  int cloud_size = vertical_scans * horizontal_scans;

  seg_pcl.start_col_idx.assign(vertical_scans, 0);
  seg_pcl.end_col_idx.assign(vertical_scans, 0);
  seg_pcl.is_ground.assign(cloud_size, false);
  seg_pcl.col_idx.assign(cloud_size, 0);
  seg_pcl.range.assign(cloud_size, 0);
}

void Mapper3D::localPCLMap(const std::vector<point>& pcl,
                           std::vector<Corner>&      out_corners,
                           Plane&                    out_groundplane,
                           const std::string&        sensor)
{
  std::vector<point> transformed_pcl;
  if (sensor == "velodyne") {
    // Set velodyne configuration parameters
    planes_th               = static_cast<float>(60.) * DEGREE_TO_RAD;
    ground_th               = static_cast<float>(10.) * DEGREE_TO_RAD;
    edge_threshold          = 0.1;
    vertical_scans          = 16;
    horizontal_scans        = 1800;
    ground_scan_idx         = 7;
    segment_valid_point_num = 5;
    segment_valid_line_num  = 3;
    vertical_angle_bottom   = static_cast<float>(15. + 0.1) * DEGREE_TO_RAD;
    ang_res_x               = static_cast<float>(0.2) * DEGREE_TO_RAD;
    ang_res_y               = static_cast<float>(2.) * DEGREE_TO_RAD;

    // Reset global variables and members
    reset();

    // Range image projection
    const size_t cloud_size = pcl.size();
    transformed_pcl.resize(vertical_scans * horizontal_scans);
    for (size_t i = 0; i < cloud_size; ++i) {
      point m_pt = pcl[i];

      float range = m_pt.norm3D();

      // find the row and column index in the image for this point
      float vertical_angle =
          std::atan2(m_pt.z, std::sqrt(m_pt.x * m_pt.x + m_pt.y * m_pt.y));

      int row_idx =
          static_cast<int>((vertical_angle + vertical_angle_bottom) / ang_res_y);
      if (row_idx < 0 || row_idx >= vertical_scans) {
        continue;
      }

      float horizon_angle = std::atan2(m_pt.x, m_pt.y);

      int column_idx = static_cast<int>(
          -round((horizon_angle - M_PI_2) / ang_res_x) + horizontal_scans / 2.);

      if (column_idx >= horizontal_scans) {
        column_idx -= horizontal_scans;
      }

      if (column_idx < 0 || column_idx >= horizontal_scans) {
        continue;
      }

      if (range < 1.0) {
        continue;
      }

      range_mat(row_idx, column_idx) = range;

      size_t idx           = column_idx + row_idx * horizontal_scans;
      transformed_pcl[idx] = m_pt;
    }
  } else {

  }

  // - GROUND PLANE
  Plane gplane_unfilt;
  groundRemoval(transformed_pcl, gplane_unfilt);
  // Filter outliers using RANSAC
  ransac(gplane_unfilt, out_groundplane);

  // - OTHER PLANES
  std::vector<PlanePoint> cloud_seg;
  cloudSegmentation(transformed_pcl, cloud_seg);

  //- Feature extraction and publication
  extractCorners(cloud_seg, out_corners);

  // Convert features to base_link referential frame
  pose tf_pose;
  TF   tf;

  if (sensor == "velodyne") {
    tf_pose = pose(vel2base_x,
                   vel2base_y,
                   vel2base_z,
                   vel2base_roll,
                   vel2base_pitch,
                   vel2base_yaw);

    std::array<float, 9> tf_rot{};
    tf_pose.toRotMatrix(tf_rot);
    tf = TF(tf_rot, std::array<float, 3>{vel2base_x, vel2base_y, vel2base_z});
  } else {
    tf_pose = pose(cam2base_x,
                   cam2base_y,
                   cam2base_z,
                   cam2base_roll,
                   cam2base_pitch,
                   cam2base_yaw);

    std::array<float, 9> tf_rot{};
    tf_pose.toRotMatrix(tf_rot);
    tf = TF(tf_rot, std::array<float, 3>{cam2base_x, cam2base_y, cam2base_z});
  }

  for (auto& pt : out_groundplane.points) {
    pt = pt * tf.inverse();
  }
  for (auto& corner : out_corners) {
    corner.pos = corner.pos * tf.inverse();
  }
  // -------------------------------------------
}

void Mapper3D::globalCornerMap(const std::vector<Corner>& corners,
                               const pose&                robot_pose,
                               OccupancyMap&              grid_map) const
{
  // ------ Convert robot pose into homogeneous transformation
  std::array<float, 9> Rot{};
  robot_pose.toRotMatrix(Rot);
  std::array<float, 3> trans = {robot_pose.x, robot_pose.y, robot_pose.z};

  // ------ Insert corner into the grid map
  for (const auto& corner : corners) {
    // - First convert them to map's referential using the robot pose
    point m_pt;
    m_pt.x = corner.pos.x * Rot[0] + corner.pos.y * Rot[1] + corner.pos.z * Rot[2] +
             trans[0];
    m_pt.y = corner.pos.x * Rot[3] + corner.pos.y * Rot[4] + corner.pos.z * Rot[5] +
             trans[1];
    m_pt.z = corner.pos.x * Rot[6] + corner.pos.y * Rot[7] + corner.pos.z * Rot[8] +
             trans[2];

    // - Then, look for correspondences in the local map
    Corner correspondence{};
    float  best_correspondence = correspondence_threshold;
    bool   found               = false;
    for (const auto& m_corner : grid_map(m_pt.x, m_pt.y).corner_features) {
      float dist_min = m_pt.distance(m_corner.pos);

      if (dist_min < best_correspondence) {
        correspondence      = m_corner;
        best_correspondence = dist_min;
        found               = true;
      }
    }

    // Only search in the adjacent cells if we do not find in the source cell
    if (!found) {
      std::vector<Cell> adjacents;
      grid_map.getAdjacent(m_pt.x, m_pt.y, 2, adjacents);
      for (const auto& m_cell : adjacents) {
        for (const auto& m_corner : m_cell.corner_features) {
          float dist_min = m_pt.distance(m_corner.pos);
          if (dist_min < best_correspondence) {
            correspondence      = m_corner;
            best_correspondence = dist_min;
            found               = true;
          }
        }
      }
    }

    // - Then, insert the corner into the grid map
    if (found) {
      point  new_pt = (m_pt + correspondence.pos) / 2.;
      Corner new_corner(new_pt, corner.which_plane);
      grid_map.update(correspondence, new_corner);
    } else {
      Corner new_corner(m_pt, corner.which_plane);
      grid_map.insert(new_corner);
    }
  }
}

void Mapper3D::groundRemoval(const std::vector<point>& in_pts, Plane& out_pcl)
{
  // _ground_mat
  // -1, no valid info to check if ground of not
  //  0, initial value, after validation, means not ground
  //  1, ground
  for (int j = 0; j < horizontal_scans; j++) {
    for (int i = 0; i < ground_scan_idx; i++) {
      int lower_idx = j + i * horizontal_scans;
      int upper_idx = j + (i + 1) * horizontal_scans;

      point upper_pt = in_pts[upper_idx];
      point lower_pt = in_pts[lower_idx];

      if (range_mat(i, j) == -1 || range_mat(i + 1, j) == -1) {
        // no info to check, invalid points
        ground_mat(i, j) = -1;
        continue;
      }

      float dX = upper_pt.x - lower_pt.x;
      float dY = upper_pt.y - lower_pt.y;
      float dZ = upper_pt.z - lower_pt.z;

      float vertical_angle = std::atan2(dZ, std::sqrt(dX * dX + dY * dY));

      if (vertical_angle <= ground_th) {
        ground_mat(i, j)     = 1;
        ground_mat(i + 1, j) = 1;
        label_mat(i, j)      = -1;
        label_mat(i + 1, j)  = -1;

        out_pcl.points.push_back(lower_pt);
        out_pcl.points.push_back(upper_pt);
      }
    }
  }
}

bool Mapper3D::ransac(const Plane& in_plane, Plane& out_plane) const
{
  int   max_idx       = in_plane.points.size();
  int   min_idx       = 0;
  int   max_tries     = 1000;
  int   c_max_inliers = 0;
  float best_a        = 0.;
  float best_b        = 0.;
  float best_c        = 0.;
  float best_d        = 0.;

  for (int i = 0; i < max_iters; i++) {
    // Declare private point cloud to store current solution
    std::vector<point> m_pcl;

    // Reset number of inliers in each iteration
    int num_inliers = 0;

    // Randomly select three points that cannot be cohincident
    // TODO (André Aguiar): Also check if points are collinear
    bool found_valid_pts = false;
    int  n               = 0;
    int  idx1, idx2, idx3;
    while (!found_valid_pts) {
      idx1 = std::rand() % (max_idx - min_idx + 1) + min_idx;
      idx2 = std::rand() % (max_idx - min_idx + 1) + min_idx;
      idx3 = std::rand() % (max_idx - min_idx + 1) + min_idx;

      if (idx1 != idx2 && idx1 != idx3 && idx2 != idx3)
        found_valid_pts = true;

      n++;
      if (n > max_tries)
        break;
    }

    if (!found_valid_pts) {
      std::cout << "WARNING (ransac): No valid set of points found ... "
                << std::endl;
      return false;
    }

    // Declarate the 3 points selected on this iteration
    point pt1 = point(
        in_plane.points[idx1].x, in_plane.points[idx1].y, in_plane.points[idx1].z);
    point pt2 = point(
        in_plane.points[idx2].x, in_plane.points[idx2].y, in_plane.points[idx2].z);
    point pt3 = point(
        in_plane.points[idx3].x, in_plane.points[idx3].y, in_plane.points[idx3].z);

    // Extract the plane hessian coefficients
    vector3D v1(pt2, pt1);
    vector3D v2(pt3, pt1);
    vector3D abc = v1.cross(v2);
    float    a   = abc.x;
    float    b   = abc.y;
    float    c   = abc.z;
    float    d   = -(a * pt1.x + b * pt1.y + c * pt1.z);

    for (const auto& m_pt : in_plane.points) {
      // Compute the distance each point to the plane - from
      // https://www.geeksforgeeks.org/distance-between-a-point-and-a-plane-in-3-d/
      auto norm = std::sqrt(a * a + b * b + c * c);
      if (std::fabs(a * m_pt.x + b * m_pt.y + c * m_pt.z + d) / norm <
          dist_threshold) {
        num_inliers++;
        m_pcl.push_back(m_pt);
      }
    }

    if (num_inliers > c_max_inliers) {
      c_max_inliers = num_inliers;

      best_a = a;
      best_b = b;
      best_c = c;
      best_d = d;

      out_plane.points.clear();
      out_plane.points = m_pcl;
    }
  }

  // -------------------------------------------------------------------------------
  // ----- Use PCA to refine th normal vector using all the inliers
  // -------------------------------------------------------------------------------
  // - 1st: assemble data matrix
  Eigen::MatrixXf data_mat;
  data_mat.conservativeResize(out_plane.points.size(), 3);
  for (size_t i = 0; i < out_plane.points.size(); i++) {
    point                      pt = out_plane.points[i];
    Eigen::Matrix<float, 1, 3> pt_mat(pt.x, pt.y, pt.z);
    data_mat.block<1, 3>(i, 0) = pt_mat;
  }
  // - 2nd: calculate mean and subtract it to the data matrix
  Eigen::MatrixXf centered_mat = data_mat.rowwise() - data_mat.colwise().mean();
  // - 3rd: calculate covariance matrix
  Eigen::MatrixXf covariance_mat = (centered_mat.adjoint() * centered_mat);
  // - 4rd: calculate eigenvectors and eigenvalues of the covariance matrix
  Eigen::SelfAdjointEigenSolver<Eigen::MatrixXf> eigen_solver(covariance_mat);
  const Eigen::VectorXf& eigen_values  = eigen_solver.eigenvalues();
  Eigen::MatrixXf        eigen_vectors = eigen_solver.eigenvectors();

  vector3D normal(
      eigen_vectors.col(0)[0], eigen_vectors.col(0)[1], eigen_vectors.col(0)[2]);
  if (normal.z < 0) {
    vector3D             m_vec = normal;
    std::array<float, 9> Rot{};
    pose                 transf(0., 0., 0., 0., M_PI, 0.);
    transf.toRotMatrix(Rot);

    m_vec.x = normal.x * Rot[0] + normal.y * Rot[1] + normal.z * Rot[2];
    m_vec.y = normal.x * Rot[3] + normal.y * Rot[4] + normal.z * Rot[5];
    m_vec.z = normal.x * Rot[6] + normal.y * Rot[7] + normal.z * Rot[8];

    normal = m_vec;
  }

  normal.normalize();
  out_plane.normal = normal;

  return c_max_inliers > 0;
}

void Mapper3D::cloudSegmentation(const std::vector<point>& in_pts,
                                 std::vector<PlanePoint>&  out_plane_pts)
{
  // Segmentation process
  int label = 1;
  for (int i = 0; i < vertical_scans; i++) {
    for (int j = 0; j < horizontal_scans; j++) {
      if (label_mat(i, j) == 0 && range_mat(i, j) != -1)
        labelComponents(i, j, in_pts, label);
    }
  }

  // Extract segmented cloud for visualization
  int seg_cloud_size = 0;
  for (int i = 0; i < vertical_scans; i++) {
    seg_pcl.start_col_idx[i] = seg_cloud_size - 1 + 5;
    for (int j = 0; j < horizontal_scans; j++) {
      if (label_mat(i, j) > 0 && label_mat(i, j) != 999999) {
        // Save segmented cloud into a pcl
        point      pt = in_pts[j + i * horizontal_scans];
        PlanePoint m_ppoint(pt, label_mat(i, j));
        out_plane_pts.push_back(m_ppoint);
        // ------------------------------------------
        // Save segmented cloud in the given structure
        seg_pcl.col_idx[seg_cloud_size] = j;
        seg_pcl.range[seg_cloud_size]   = range_mat(i, j);
        seg_cloud_size++;
        // ------------------------------------------
      }
    }
    seg_pcl.end_col_idx[i] = seg_cloud_size - 1 - 5;
  }
}

void Mapper3D::labelComponents(const int&                row,
                               const int&                col,
                               const std::vector<point>& in_pts,
                               int&                      label)
{
  using Coord2D = Eigen::Vector2i;
  std::deque<Coord2D> queue;
  std::deque<Coord2D> global_queue;

  queue.emplace_back(row, col);
  global_queue.emplace_back(row, col);

  std::vector<bool> line_count_flag(vertical_scans, false);

  // - Define neighborhood
  const Coord2D neighbor_it[4] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};

  while (!queue.empty()) {
    // Evaluate front element of the queue and pop it
    Coord2D from_idx = queue.front();
    queue.pop_front();

    // Mark popped point as belonging to the segment
    label_mat(from_idx.x(), from_idx.y()) = label;

    // Compute point from range image
    float d1 = range_mat(from_idx.x(), from_idx.y());

    // Loop through all the neighboring grids of popped grid
    for (const auto& iter : neighbor_it) {
      // Compute new index
      int c_idx_x = from_idx.x() + iter.x();
      int c_idx_y = from_idx.y() + iter.y();

      // Check if index is within the boundary
      if (c_idx_x < 0 || c_idx_x >= vertical_scans)
        continue;
      if (c_idx_y < 0)
        c_idx_y = horizontal_scans - 1;
      if (c_idx_y >= horizontal_scans)
        c_idx_y = 0;

      // Prevent infinite loop (caused by put already examined point back)
      if (label_mat(c_idx_x, c_idx_y) != 0)
        continue;

      // Compute point from range image
      float d2 = range_mat(c_idx_x, c_idx_y);

      float dmax = std::max(d1, d2);
      float dmin = std::min(d1, d2);

      // Compute angle between the two points
      float alpha = (iter.x() == 0) ? ang_res_x : ang_res_y;

      // Compute beta and check if points belong to the same segment
      auto beta =
          std::atan2((dmin * std::sin(alpha)), (dmax - dmin * std::cos(alpha)));
      if (beta > planes_th) {
        queue.emplace_back(c_idx_x, c_idx_y);
        global_queue.emplace_back(c_idx_x, c_idx_y);

        label_mat(c_idx_x, c_idx_y) = label;
        line_count_flag[c_idx_x]    = true;
      }
    }
  }

  // Check if this segment is valid
  bool feasible_segment = false;
  if (global_queue.size() >= 30) {
    feasible_segment = true;
  } else if (global_queue.size() >= segment_valid_point_num) {
    int line_count = 0;
    for (int i = 0; i < vertical_scans; i++) {
      if (line_count_flag[i])
        line_count++;
    }

    if (line_count >= segment_valid_line_num)
      feasible_segment = true;
  }

  if (feasible_segment) {
    label++;
  } else {
    for (auto& i : global_queue) label_mat(i.x(), i.y()) = 999999;
  }
}

void Mapper3D::extractCorners(const std::vector<PlanePoint>& in_plane_pts,
                              std::vector<Corner>&           out_corners)
{
  // -------------------------------------------------------------------------------
  // ----- Compute cloud smoothness
  // -------------------------------------------------------------------------------
  int                       m_cloud_size = in_plane_pts.size();
  std::vector<smoothness_t> cloud_smoothness(vertical_scans * horizontal_scans);
  std::vector<int>          neighbor_picked(vertical_scans * horizontal_scans);
  for (int i = 5; i < m_cloud_size - 5; i++) {
    // Compute smoothness and save it
    float diff_range =
        seg_pcl.range[i - 5] + seg_pcl.range[i - 4] + seg_pcl.range[i - 3] +
        seg_pcl.range[i - 2] + seg_pcl.range[i - 1] + seg_pcl.range[i + 1] +
        seg_pcl.range[i + 2] + seg_pcl.range[i + 3] + seg_pcl.range[i + 4] +
        seg_pcl.range[i + 5] - 10 * seg_pcl.range[i];

    cloud_smoothness[i].value = diff_range * diff_range;
    cloud_smoothness[i].idx   = i;

    // Reset neighborhood flag array
    neighbor_picked[i] = 0;
  }

  // -------------------------------------------------------------------------------
  // ----- Extract features from the 3D cloud
  // -------------------------------------------------------------------------------
  for (int i = 0; i < vertical_scans; i++) {
    for (int k = 0; k < 6; k++) {
      // Compute start and end indexes of the sub-region
      int sp =
          (seg_pcl.start_col_idx[i] * (6 - k) + (seg_pcl.end_col_idx[i] * k)) / 6;
      int ep =
          (seg_pcl.start_col_idx[i] * (5 - k) + (seg_pcl.end_col_idx[i] * (k + 1))) /
              6 -
          1;

      if (sp >= ep)
        continue;

      // Sort smoothness values for the current sub-region
      std::sort(
          cloud_smoothness.begin() + sp, cloud_smoothness.begin() + ep, by_value());

      // -- Extract edge features
      // -----------------------------------------------------
      int picked_counter = 0;
      for (int l = ep; l >= sp; l--) {
        int idx = cloud_smoothness[l].idx;

        // Check if the current point is an edge feature
        if (neighbor_picked[idx] == 0 &&
            cloud_smoothness[l].value > edge_threshold) {
          picked_counter++;
          if (picked_counter <= 20) {
            Corner m_corner(in_plane_pts[idx].pos, in_plane_pts[idx].which_plane);
            out_corners.push_back(m_corner);
          } else {
            break;
          }

          // Mark neighbor points to reject as future features
          neighbor_picked[idx] = 1;
          for (int m = 1; m <= 5; m++) {
            if (idx + m >= seg_pcl.col_idx.size())
              continue;
            int col_diff =
                std::abs(seg_pcl.col_idx[idx + m] - seg_pcl.col_idx[idx + m - 1]);
            if (col_diff > 10)
              break;
            else
              neighbor_picked[idx + m] = 1;
          }
          for (int m = -1; m >= -5; m--) {
            if (idx + m < 0)
              continue;
            int col_diff =
                std::abs(seg_pcl.col_idx[idx + m] - seg_pcl.col_idx[idx + m + 1]);
            if (col_diff > 10)
              break;
            else
              neighbor_picked[idx + m] = 1;
          }
        }
      }
      // -----------------------------------------------------
    }
  }
}

// -------------------------------------------------------------------------------

void Mapper3D::pixel2base(const point& in_pt,
                          const float& depth,
                          point&       out_pt) const
{
  // Project 2D pixel into a 3D Point using the stereo depth information
  float x_cam = (in_pt.x - cx) * (depth / fx);
  float y_cam = (in_pt.y - cy) * (depth / fy);
  float z_cam = depth;
  point pt_cam(x_cam, y_cam, z_cam);

  // Compute camera-world axis transformation matrix
  pose                 cam2world(0., 0., 0, -M_PI / 2., 0., -M_PI / 2.);
  std::array<float, 9> c2w_rot{};
  cam2world.toRotMatrix(c2w_rot);
  TF cam2world_tf(c2w_rot, std::array<float, 3>{0., 0., 0.});

  // Align world and camera axis
  point wpoint = pt_cam * cam2world_tf;

  // Compute camera-to-base transformation matrix
  pose cam2base(cam2base_x,
                cam2base_y,
                cam2base_z,
                cam2base_roll,
                cam2base_pitch,
                cam2base_yaw);

  std::array<float, 9> c2b_rot{};
  cam2base.toRotMatrix(c2b_rot);
  TF cam2base_tf(c2b_rot, std::array<float, 3>{cam2base_x, cam2base_y, cam2base_z});

  // Transform camera point to base_link
  out_pt = wpoint * cam2base_tf.inverse();
}

} // namespace vineslam
