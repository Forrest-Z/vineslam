#include "icp.hpp"

#include <cmath>

namespace vineslam
{

ICP::ICP(const Parameters& params)
{
  // Set the default stop criteria parameters
  // - they can (and should) be overwritten from the outside call (!)
  max_iters       = params.icp_max_iters;
  tolerance       = 1e-3;
  dist_threshold  = params.icp_distance_threshold;
  reject_outliers = params.icp_reject_outliers;

  // Initialize homogeneous transformation
  R = {1., 0., 0., 0., 1., 0., 0., 0., 1.};
  t = {0., 0., 0.};
}

bool ICP::align(TF tf, float& rms_error, std::vector<ImageFeature>& aligned)
{
  if (source.empty()) {
    std::cout << "WARNING (ICP::align): source cloud empty. Returning first guess."
              << std::endl;
    return true;
  }

  // Initialize stop criteria parameters
  int   n_iters    = 0;
  float delta_dist = 1e6;
  // Initialize homogeneous transformation
  Eigen::Matrix3f Rot;
  Eigen::Vector3f trans;
  stdToEig(tf.R, Rot);
  stdToEig(tf.t, trans);

  // Perform first iteration and save the error
  float p_rms_error;
  step(Rot, trans, p_rms_error);
  n_iters++;

  // Boolean to check if we found a suitable solution
  bool found_solution = false;

  rms_error = 0.;
  while (n_iters < max_iters && delta_dist > tolerance) {
    if (step(Rot, trans, rms_error)) {
      delta_dist  = std::fabs(rms_error - p_rms_error);
      p_rms_error = rms_error;

      found_solution = true;
    }

    n_iters++;
  }

  if (!found_solution) // invalid iteration
  {
    std::cout << "WARNING ICP::align: Scan matcher failed - none valid iteration..."
              << std::endl;
    return false;
  }

  // Save homogeneous transformation solution
  eigToStd(Rot, R);
  eigToStd(trans, t);

  // Check if ICP produced a big step. If so, invalid iteration
  TF   tf_res(R, t);
  TF   tf_delta = tf.inverse() * tf_res;
  pose delta_p(tf_delta.R, tf_delta.t);
  if (std::fabs(delta_p.x) > 0.3 || std::fabs(delta_p.y) > 0.3 ||
      std::fabs(delta_p.z) > 0.3 || std::fabs(delta_p.roll) > 0.35 ||
      std::fabs(delta_p.pitch) > 0.35 || std::fabs(delta_p.yaw) > 0.35) {
    std::cout << "WARNING ICP::align: Huge jump detected on ICP - considering "
                 "iteration as invalid..."
              << std::endl;
    return false;
  }

  // Compute aligned point cloud
  aligned.resize(source.size());
  for (size_t i = 0; i < aligned.size(); i++) {
    aligned[i] = source[i];

    point spt = source[i].pos;
    point apt;
    apt.x = spt.x * R[0] + spt.y * R[1] + spt.z * R[2] + t[0];
    apt.y = spt.x * R[3] + spt.y * R[4] + spt.z * R[5] + t[1];
    apt.z = spt.x * R[6] + spt.y * R[7] + spt.z * R[8] + t[2];

    aligned[i].pos = apt;
  }

  return true;
}

bool ICP::align(float& rms_error, std::vector<ImageFeature>& aligned)
{
  // Set rotation to identity and translation to 0
  std::array<float, 9> m_R = {1., 0., 0., 0., 1., 0., 0., 0., 1.};
  std::array<float, 3> m_t{};

  TF tf(m_R, m_t);

  return align(tf, rms_error, aligned);
}

bool ICP::step(Eigen::Matrix3f& m_R, Eigen::Vector3f& m_t, float& rms_error)
{
  // Initialize source and target means
  Eigen::Vector3f target_mean(0., 0., 0.);
  Eigen::Vector3f source_mean(0., 0., 0.);

  // Declare target and source matrices to save the correspondences
  Eigen::Matrix3Xf target_pts;
  Eigen::Matrix3Xf source_pts;
  target_pts.resize(3, source.size());
  source_pts.resize(3, source.size());

  // Inlier correspondences
  Eigen::Matrix3Xf inliers_tpoints;
  Eigen::Matrix3Xf inliers_spoints;

  // Declare matrices to store the result
  Eigen::Matrix3f delta_R;
  Eigen::Vector3f delta_t;

  // Iterator that will count the number of valid iterations
  int32_t j = 0;
  // Iterator that will count the number inliers
  int32_t nsamples = 0;

  // Arrays to all the correspondences errors
  derrorvec.clear(); // clear error array
  serrorvec.clear(); // clear error array

  for (const auto& m_feature : source) {
    // Convert feature into the target reference frame using current [R|t] solution
    Eigen::Vector3f fsource(m_feature.pos.x, m_feature.pos.y, m_feature.pos.z);
    Eigen::Vector3f ftransformed = m_R * fsource + m_t;

    // Find nearest neighbor of the current point
    ImageFeature _ftarget;
    ImageFeature _ftransformed = m_feature;
    _ftransformed.pos =
        point(ftransformed(0, 0), ftransformed(1, 0), ftransformed(2, 0));
    // Save minimum distance found (usually for the descriptor)
    float sdist = 1e6;
    float ddist = 1e6;
    if (!target->findNearest(_ftransformed, _ftarget, sdist, ddist)) {
      continue;
    }

    // Save source and target points
    Eigen::Vector3f ftarget(_ftarget.pos.x, _ftarget.pos.y, _ftarget.pos.z);
    target_pts.block<3, 1>(0, j) = ftarget;
    source_pts.block<3, 1>(0, j) = ftransformed;

    // Remove (or not) outliers using a displacement threshold on the descriptor
    // space
    // -----------------------------------------------------------------------------
    if (sdist < dist_threshold || !reject_outliers) {
      nsamples = inliers_spoints.cols() + 1;

      inliers_tpoints.conservativeResize(3, nsamples);
      inliers_spoints.conservativeResize(3, nsamples);
      // Push back inliers
      inliers_tpoints.block<3, 1>(0, nsamples - 1) = target_pts.block<3, 1>(0, j);
      inliers_spoints.block<3, 1>(0, nsamples - 1) = source_pts.block<3, 1>(0, j);

      // Accumulate the results to then compute the pointwise mean
      target_mean += inliers_tpoints.block<3, 1>(0, nsamples - 1);
      source_mean += inliers_spoints.block<3, 1>(0, nsamples - 1);

      // Store correspondences errors just for inliers
      serrorvec.push_back(sdist);
      derrorvec.push_back(ddist);
    }
    // -----------------------------------------------------------------------------

    // Valid iteration
    j++;
  }

  if (j == 0) // Invalid iteration
  {
    std::cout
        << "WARNING (ICP::step): Invalid iteration - none correspondence found..."
        << std::endl;
    return false;
  }
  if (nsamples == 0) // Invalid iteration
  {
    std::cout << "WARNING (ICP::step): Invalid iteration - none inlier found..."
              << std::endl;
    return false;
  }

  // Compute center of mass of source and target point clouds
  target_mean /= static_cast<float>(nsamples);
  source_mean /= static_cast<float>(nsamples);

  // Compute pointwise difference in relation to the center of mass of each point
  // cloud
  // -------------------------------------------------------------------------------
  Eigen::MatrixXf ones;
  ones.resize(1, nsamples);
  ones.setOnes();
  // -------------------------------------------------------------------------------
  Eigen::MatrixXf tdiff;
  tdiff = target_mean * ones;
  tdiff = inliers_tpoints - tdiff;
  // -------------------------------------------------------------------------------
  Eigen::MatrixXf sdiff;
  sdiff = source_mean * ones;
  sdiff = inliers_spoints - sdiff;
  // -------------------------------------------------------------------------------

  // Compute A matrix to input the Single Value Decomposition
  Eigen::Matrix3f A = tdiff * sdiff.transpose();

  // Perform SVD to extract the rotation matrix and the translation vector
  Eigen::JacobiSVD<Eigen::Matrix3f> svd(A,
                                        Eigen::ComputeThinU | Eigen::ComputeThinV);
  delta_R = svd.matrixU() * svd.matrixV().transpose();
  delta_t = target_mean - delta_R * source_mean;

  // Compute RMS error between the target and the aligned cloud
  rms_error = 0.;
  for (int i = 0; i < nsamples; i++) {
    Eigen::Vector3f m_target_pt = inliers_tpoints.block<3, 1>(0, i);
    Eigen::Vector3f m_source_pt = inliers_spoints.block<3, 1>(0, i);

    Eigen::Vector3f m_diff = m_target_pt - (delta_R * m_source_pt + delta_t);
    rms_error += m_diff.norm();
  }
  rms_error /= static_cast<float>(nsamples);

  // Compose the transformations
  m_R = delta_R * m_R;
  m_t = delta_R * m_t + delta_t;

  return true;
}

bool ICP::score(const pose&                      spose,
                const std::vector<ImageFeature>& scloud,
                float&                           rms_error)
{
  rms_error = 0.;

  // Compute the homogeneous transformation that represents the input pose
  std::array<float, 9> Rot   = {0., 0., 0., 0., 0., 0., 0., 0., 0.};
  std::array<float, 3> trans = {spose.x, spose.y, spose.z};
  spose.toRotMatrix(Rot);

  // Transform source cloud into the maps' referential using the input pose
  std::vector<ImageFeature> transcloud;
  for (size_t i = 0; i < scloud.size(); i++) {
    transcloud[i] = scloud[i];

    point spt = scloud[i].pos;
    point apt;
    apt.x = spt.x * R[0] + spt.y * R[1] + spt.z * R[2] + t[0];
    apt.y = spt.x * R[3] + spt.y * R[4] + spt.z * R[5] + t[1];
    apt.z = spt.x * R[6] + spt.y * R[7] + spt.z * R[8] + t[2];

    transcloud[i].pos = apt;
  }

  return false;
}

inline void ICP::stdToEig(const std::array<float, 9>& m_R, Eigen::Matrix3f& Rot)
{
  Rot(0, 0) = m_R[0];
  Rot(0, 1) = m_R[1];
  Rot(0, 2) = m_R[2];
  Rot(1, 0) = m_R[3];
  Rot(1, 1) = m_R[4];
  Rot(1, 2) = m_R[5];
  Rot(2, 0) = m_R[6];
  Rot(2, 1) = m_R[7];
  Rot(2, 2) = m_R[8];
}

inline void ICP::stdToEig(const std::array<float, 3>& m_t, Eigen::Vector3f& trans)
{
  trans(0, 0) = m_t[0];
  trans(1, 0) = m_t[1];
  trans(2, 0) = m_t[2];
}

inline void ICP::eigToStd(const Eigen::Matrix3f& Rot, std::array<float, 9>& m_R)
{
  m_R[0] = Rot(0, 0);
  m_R[1] = Rot(0, 1);
  m_R[2] = Rot(0, 2);
  m_R[3] = Rot(1, 0);
  m_R[4] = Rot(1, 1);
  m_R[5] = Rot(1, 2);
  m_R[6] = Rot(2, 0);
  m_R[7] = Rot(2, 1);
  m_R[8] = Rot(2, 2);
}

inline void ICP::eigToStd(const Eigen::Vector3f& trans, std::array<float, 3>& m_t)
{
  m_t[0] = trans(0, 0);
  m_t[1] = trans(1, 0);
  m_t[2] = trans(2, 0);
}

} // namespace vineslam