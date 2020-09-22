#include "localizer.hpp"

#include <utility>

namespace vineslam
{

Localizer::Localizer(Parameters params)
    : params(std::move(params))
{
}

void Localizer::init(const pose& initial_pose)
{
  // Initialize the particle filter
  pf = new PF(params, initial_pose);

  // Compute average pose and standard deviation of the
  // first distribution
  std::vector<pose> poses;
  for (auto& particle : pf->particles) poses.push_back(particle.p);
  average_pose = pose(poses);
}

void Localizer::process(const pose&        odom,
                        const Observation& obsv,
                        OccupancyMap*      grid_map)
{
  auto before = std::chrono::high_resolution_clock::now();
  // Reset weights sum
  pf->w_sum = 0.;

  // ------------------------------------------------------------------------------
  // ---------------- Draw particles using odometry motion model
  // ------------------------------------------------------------------------------
  pf->motionModel(odom);
  // ------------------------------------------------------------------------------
  // ---------------- Update particles weights using multi-layer map
  // ------------------------------------------------------------------------------
  pf->update(obsv.landmarks,
             obsv.corners,
             obsv.vegetation_lines,
             obsv.ground_plane,
             obsv.surf_features,
             obsv.gps_pose,
             grid_map);

  // ------------------------------------------------------------------------------
  // ---------------- Normalize particle weights
  // ------------------------------------------------------------------------------
  pf->normalizeWeights();

  // ------------------------------------------------------------------------------
  // ---------------- Resample particles
  // ------------------------------------------------------------------------------
  pf->resample();

  // - Compute final robot pose using the mean of the particles poses
  std::vector<pose> poses;
  for (const auto& particle : pf->particles) poses.push_back(particle.p);
  average_pose = pose(poses);

  // - Save current control to use in the next iteration
  pf->p_odom = odom;
  auto after = std::chrono::high_resolution_clock::now();
  std::chrono::duration<float, std::milli> duration = after - before;
  std::cout << "Time elapsed on PF (msecs): " << duration.count() << std::endl
            << std::endl;
}

pose Localizer::getPose() const { return average_pose; }

void Localizer::getParticles(std::vector<pose>& in) const
{
  in.resize(pf->particles.size());
  for (size_t i = 0; i < in.size(); i++) in[i] = pf->particles[i].p;
}

} // namespace vineslam