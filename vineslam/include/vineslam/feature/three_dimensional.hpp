#pragma once

#include "feature.hpp"

namespace vineslam
{
// ---------------------------------------------------------------------------------
// ----- Point cloud medium-level corner feature
// ---------------------------------------------------------------------------------

struct Corner : public Feature
{
  Corner() = default;

  Corner(const Point& m_pt, const int& m_which_plane, const int& m_id = 0)
  {
    pos_ = m_pt;
    which_plane_ = m_which_plane;
    id_ = m_id;
    n_observations_ = 0;
  }

  int n_observations_{};
  int which_plane_{};  // sets the plane where the corner belongs
};

// ---------------------------------------------------------------------------------
// ----- Point cloud medium-level planar feature
// ---------------------------------------------------------------------------------

struct Planar : public Feature
{
  Planar() = default;

  Planar(const Point& m_pt, const int& m_which_plane, const int& m_id = 0)
  {
    pos_ = m_pt;
    which_plane_ = m_which_plane;
    id_ = m_id;
    n_observations_ = 0;
  }

  int n_observations_{};
  int which_plane_{};  // sets the plane where the corner belongs
};

// Dummy struct to represent a plane point, before corner extraction
struct PlanePoint : public Corner
{
  PlanePoint() = default;

  PlanePoint(const Point& m_pt, const int& m_which_plane)
  {
    pos_ = m_pt;
    which_plane_ = m_which_plane;
  }

  explicit PlanePoint(const Corner& m_corner)
  {
    pos_ = m_corner.pos_;
    which_plane_ = m_corner.which_plane_;
  }
};

// ---------------------------------------------------------------------------------
// ----- Point cloud medium-level plane feature
// ---------------------------------------------------------------------------------

struct Plane
{
  Plane() = default;

  Plane(const float& m_a, const float& m_b, const float& m_c, const float& m_d, const std::vector<Point>& m_points)
  {
    a_ = m_a;
    b_ = m_b;
    c_ = m_c;
    d_ = m_d;
    points_ = m_points;
  }

  // RANSAC routine
  bool ransac(const Plane& in_plane, int max_iters = 20, float dist_threshold = 0.08)
  {
    int max_idx = in_plane.points_.size();
    int min_idx = 0;
    int max_tries = 1000;
    int c_max_inliers = 0;

    for (int i = 0; i < max_iters; i++)
    {
      // Declare private point cloud to store current solution
      std::vector<Point> m_pcl;

      // Reset number of inliers in each iteration
      int num_inliers = 0;

      // Randomly select three points that cannot be cohincident
      // TODO (André Aguiar): Also check if points are collinear
      bool found_valid_pts = false;
      int n = 0;
      int idx1, idx2, idx3;
      while (!found_valid_pts)
      {
        idx1 = std::rand() % (max_idx - min_idx + 1) + min_idx;
        idx2 = std::rand() % (max_idx - min_idx + 1) + min_idx;
        idx3 = std::rand() % (max_idx - min_idx + 1) + min_idx;

        if (idx1 != idx2 && idx1 != idx3 && idx2 != idx3)
          found_valid_pts = true;

        n++;
        if (n > max_tries)
          break;
      }

      if (!found_valid_pts)
      {
        std::cout << "WARNING (ransac): No valid set of points found ... " << std::endl;
        return false;
      }

      // Declarate the 3 points selected on this iteration
      Point pt1 = Point(in_plane.points_[idx1].x_, in_plane.points_[idx1].y_, in_plane.points_[idx1].z_);
      Point pt2 = Point(in_plane.points_[idx2].x_, in_plane.points_[idx2].y_, in_plane.points_[idx2].z_);
      Point pt3 = Point(in_plane.points_[idx3].x_, in_plane.points_[idx3].y_, in_plane.points_[idx3].z_);

      // Extract the plane hessian coefficients
      Vec v1(pt2, pt1);
      Vec v2(pt3, pt1);
      Vec abc = v1.cross(v2);
      float m_a = abc.x_;
      float m_b = abc.y_;
      float m_c = abc.z_;
      float m_d = -(m_a * pt1.x_ + m_b * pt1.y_ + m_c * pt1.z_);

      for (const auto& m_pt : in_plane.points_)
      {
        // Compute the distance each point to the plane - from
        // https://www.geeksforgeeks.org/distance-between-a-point-and-a-plane-in-3-d/
        auto norm = std::sqrt(m_a * m_a + m_b * m_b + m_c * m_c);
        if (std::fabs(m_a * m_pt.x_ + m_b * m_pt.y_ + m_c * m_pt.z_ + m_d) / norm < dist_threshold)
        {
          num_inliers++;
          m_pcl.push_back(m_pt);
        }
      }

      if (num_inliers > c_max_inliers)
      {
        c_max_inliers = num_inliers;

        points_.clear();
        points_ = m_pcl;
        a_ = m_a;
        b_ = m_b;
        c_ = m_c;
        d_ = m_d;
      }
    }

    return c_max_inliers > 0;
  }

  int id_{};                     // plane identifier
  float a_{}, b_{}, c_{}, d_{};  // plane hessian coefficients
  std::vector<Point> points_;    // set of points that belong to the plane
  std::vector<Point> indexes_;   // indexes of points projected into the range image
};

}  // namespace vineslam