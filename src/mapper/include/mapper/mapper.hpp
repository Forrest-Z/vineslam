#pragma once

#include "../../utils/utils.hpp"
#include "landmark_processor.hpp"
#include <iostream>
#include <math.h>

class Estimator
{
public:
	Estimator(const Parameters& params, const LandmarkProcessor& lprocessor);
	void init();

	std::vector<Point<double>> all_sols;
	void process(std::vector<Landmark<double>>& landmarks,
	             const std::vector<Pose<double>>&     robot_poses);

	cv::Mat map;

private:
	void filterXYTheta(const std::vector<Pose<double>> robot_poses,
	                   std::vector<Pose<double>>&      filtered_poses);
	void drawMap(const std::vector<Pose<double>>& poses);
	void predict(std::vector<Landmark<double>>& landmarks,
	             const std::vector<Pose<double>>&     robot_poses);

	Parameters        params;
	LandmarkProcessor lprocessor;
	Pose<double>      prev_pose;

  std::vector<Point<double>> all_res;
};
