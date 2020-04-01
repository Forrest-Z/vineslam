#pragma once

// Include class objects
#include <landmark.hpp>
#include <math/pose6D.hpp>

// Include std members
#include <iostream>
#include <map>
#include <random>

#define PI 3.14159265359

// Struct that represents a single particle with
// - identification number
// - 6-DOF pose
// - weight
struct Particle
{
	Particle() {}
	Particle(const int& id, const pose6D& pose, const float& w)
	{
		(*this).id   = id;
		(*this).pose = pose;
		(*this).w    = w;
	}
	int    id;
	pose6D pose;
	float  w;
};

static std::ostream& operator<<(std::ostream& o, const Particle& p)
{
	o << "Particle " << p.id << ":\n" << p.pose << p.w << "\n\n";
	return o;
}

class PF
{
public:
	// Class constructor
	// - initializes the total set of particles
	PF(const int& n_particles, const pose6D& initial_pose);

	// Global function that handles all the particle filter processes
	void process(const pose6D& odom, const std::vector<float>& bearings,
	             const std::vector<float>&             depths,
	             const std::map<int, Landmark<float>>& map);

	// Export the array of particles
	void getParticles(std::vector<Particle>& in);

private:
	// Prediction step - particles inovation using a motion model
	void predict(const pose6D& odom);
	// Correction step
	// - calculate a local map for each particle
	// - update particle weight using the difference between the local and
	//   the global map
	// - normalize the weights
	void correct(const std::vector<float>&             bearings,
	             const std::vector<float>&             depths,
	             const std::map<int, Landmark<float>>& map);
	// Resampling over all particles
	void resample();

	// Auxiliar function that normalizes an angle in the [-pi,pi] range
	float normalizeAngle(const float& angle)
	{
		return (std::fmod(angle + PI, 2 * PI) - PI);
	}

	// Previous odometry control
	pose6D p_odom;
	// Array of particles
	std::vector<Particle> particles;
};
