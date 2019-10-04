#include "../include/mapper/landmark_processor.hpp"

LandmarkProcessor::LandmarkProcessor(const Parameters& params) : params(params)
{
}

void LandmarkProcessor::updatePoses(const std::vector<Point<double>>& poses)
{
	lp_pose = lc_pose;
	lc_pose = poses;
}

void LandmarkProcessor::matchLandmarks(const Pose<double>& robot_pose)
{
	matches.clear();
	bool was_found;

	for (size_t i = 0; i < lc_pose.size(); i++) {
		was_found = false;
		for (size_t j = 0; j < landmarks.size(); j++) {
			int           n      = landmarks[j].image_pos.size() - 1;
			Point<double> p_pose = landmarks[j].image_pos[n];

			if (std::fabs(lc_pose[i].x - p_pose.x) < params.match_box) {
				Line<double> lp_line = computeLine(p_pose);
				Line<double> lc_line = computeLine(lc_pose[i]);

				matches.push_back(Match<double>(p_pose, lc_pose[i], lp_line, lc_line));
				landmarks[j].image_pos.push_back(lc_pose[i]);
				landmarks[j].r_pose.push_back(robot_pose);

				was_found = true;
				break;
			}
		}

		if (was_found == false) {
			landmarks.push_back(Landmark<double>(landmarks.size(), lc_pose[i]));
			landmarks[landmarks.size() - 1].r_pose.push_back(robot_pose);
		}
	}
}

Line<double> LandmarkProcessor::computeLine(const Point<double>& landmark)
{
	double orientation =
	    -(params.h_fov / params.width) * (params.width / 2 - landmark.x);

	Point<double> p1(0, 0);
	Point<double> p2(20 * cos(orientation), 20 * sin(orientation));

	return Line<double>(p1, p2);
}

Line<double> LandmarkProcessor::computeLine(const Point<double>& landmark,
                                            const double&        phi)
{
	double orientation =
	    -(params.h_fov / params.width) * (params.width / 2 - landmark.x);

	Point<double> p1(0, 0);
	Point<double> p2(20 * cos(orientation - phi), 20 * sin(orientation - phi));

	return Line<double>(p1, p2);
}

Line<double> LandmarkProcessor::projectLine(const Point<double>& pos,
                                            const Point<double>& delta_p,
                                            const double&        delta_th)
{
	/* First rotate the line */
	Line<double> l = computeLine(pos, delta_th);
	/* Then, translate the line */
	Point<double> p1 = l.p1 + delta_p;
	Point<double> p2 = l.p2 + delta_p;

	return Line<double>(p1, p2);
}
