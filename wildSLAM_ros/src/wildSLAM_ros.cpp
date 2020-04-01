#include "../include/wildSLAM_ros.hpp"

void wildSLAM_ros::SLAMNode::odomListener(const nav_msgs::OdometryConstPtr& msg)
{
	// Convert odometry msg to pose msg
	tf::Pose            pose;
	geometry_msgs::Pose odom_pose = (*msg).pose.pose;
	tf::poseMsgToTF(odom_pose, pose);

	// Check if yaw is NaN
	float yaw = tf::getYaw(pose.getRotation());
	if (yaw != yaw)
		yaw = 0;

	// If it is the first iteration - initialize the Pose
	// relative to the previous frame
	if (init == true) {
		p_odom.x   = (*msg).pose.pose.position.x;
		p_odom.y   = (*msg).pose.pose.position.y;
		p_odom.yaw = yaw;
		odom.yaw   = yaw;
		return;
	}

	// Integrate odometry pose to convert to the map frame
	odom.x += (*msg).pose.pose.position.x - p_odom.x;
	odom.y += (*msg).pose.pose.position.y - p_odom.y;
	odom.z     = 0;
	odom.roll  = 0;
	odom.pitch = 0;
	odom.yaw   = yaw;

	// Save current odometry pose to use in the next iteration
	p_odom.x   = (*msg).pose.pose.position.x;
	p_odom.y   = (*msg).pose.pose.position.y;
	p_odom.yaw = yaw;
}

void wildSLAM_ros::SLAMNode::callbackFct(
    const sensor_msgs::ImageConstPtr&            left_image,
    const sensor_msgs::ImageConstPtr&            depth_image,
    const vision_msgs::Detection2DArrayConstPtr& dets)
{
	// Declaration of the arrays that will constitute the SLAM observations
	std::vector<int>   labels;
	std::vector<float> bearings;
	std::vector<float> depths;

	// Loop over all the bounding box detections
	for (size_t i = 0; i < (*dets).detections.size(); i++) {
		// Load a single bounding box detection
		vision_msgs::BoundingBox2D m_bbox = (*dets).detections[i].bbox;

		// Calculate the depth of the detected object
		float depth =
		    computeDepth(*depth_image, m_bbox.center.x - m_bbox.size_x / 2,
		                 m_bbox.center.y - m_bbox.size_y / 2,
		                 m_bbox.center.x + m_bbox.size_x / 2,
		                 m_bbox.center.y + m_bbox.size_y / 2);

		// Check if the calculated depth is valid
		if (depth == -1)
			continue;

		// Calculate the correspondent bearing observations
		float column  = m_bbox.center.x;
		float bearing = (-h_fov / img_width) * (img_width / 2 - column);

		// Insert the measures in the observations arrays
		labels.push_back((*dets).detections[i].results[0].id);
		depths.push_back(depth);
		bearings.push_back(bearing);
	}

	if (init == true && bearings.size() > 1) {
		// Initialize the localizer and get first particles distribution
		(*localizer).init(pose6D(0, 0, 0, 0, 0, odom.yaw));
		pose6D robot_pose = (*localizer).getPose();

		// Initialize the mapper2D
		(*mapper2D).init(robot_pose, bearings, depths, labels);

		// Initialize the mapper3D
		(*mapper3D).init();

		// Get first map2D
		map2D = (*mapper2D).getMap();

		init = false;
	}
	else if (init == false) {
		// Execute the localization procedure
		(*localizer).process(odom, bearings, depths, map2D);
		pose6D robot_pose = (*localizer).getPose();

		// Convert ROS image to OpenCV image
		cv::Mat m_img =
		    cv_bridge::toCvCopy(left_image, sensor_msgs::image_encodings::BGR8)
		        ->image;
		// Loop over the image
		std::vector<std::array<uint8_t, 3>> rgb_array;
		rgb_array.resize(m_img.cols * m_img.rows);
		for (int i = 0; i < m_img.cols; i++) {
			for (int j = 0; j < m_img.rows; j++) {
				// Get BRG values for the current index
				cv::Point3_<uchar>* p = m_img.ptr<cv::Point3_<uchar>>(j, i);
				// Calculate the 1D array index
				int idx = i + j * m_img.cols;
				// Store the RGB value
				std::array<uint8_t, 3> m_rgb;
				m_rgb[0] = (*p).z;
				m_rgb[1] = (*p).y;
				m_rgb[2] = (*p).x;
				// Save the RGB value into the multi array
				rgb_array[idx] = m_rgb;
			}
		}

		// Execute the 3D map estimation
		float* all_depths = (float*)(&(*depth_image).data[0]);
		(*mapper3D).process(all_depths, rgb_array, robot_pose, *dets);

		// Publish 3D point clouds
		// publish3DRawMap((*depth_image).header);
		publish3DTrunkMap((*depth_image).header);

		// Execute the 2D map estimation
		(*mapper2D).process(robot_pose, bearings, depths, labels);
		// Get the curretn 2D map
		map2D = (*mapper2D).getMap();
		// Publish the 2D map
		publish2DMap((*depth_image).header, robot_pose);

		// Publish robot pose
		geometry_msgs::PoseStamped pose;
		pose.header          = (*depth_image).header;
		pose.header.frame_id = "map";
		pose_publisher.publish(pose);

		// Convert robot pose to tf::Transform corresponding
		// to the camera to map transformation
		tf::Quaternion q;
		q.setRPY(robot_pose.roll, robot_pose.pitch, robot_pose.yaw);
		q.normalize();
		tf::Transform cam2map;
		cam2map.setRotation(q);
		cam2map.setOrigin(tf::Vector3(robot_pose.x, robot_pose.y, robot_pose.z));

		// Convert wildSLAM pose to ROS pose
		// ...

		// Publish cam-to-map tf::Transform
		static tf::TransformBroadcaster br;
		br.sendTransform(
		    tf::StampedTransform(cam2map, pose.header.stamp, "map", "cam"));
	}
}

float wildSLAM_ros::SLAMNode::computeDepth(const sensor_msgs::Image& depth_img,
                                           const int& xmin, const int& ymin,
                                           const int& xmax, const int& ymax)
{
	// Declare array with all the disparities computed
	float* depths = (float*)(&(depth_img).data[0]);

	float range_min = 0.01;
	float range_max = 10.0;

  double min_value = 50.0;
  
  Point<double> min_index;

	std::vector<float> depth_array;
	for (int x = xmin; x < xmax; x++) {
		for (int y = ymin; y < ymax; y++) {
			int idx = x + depth_img.width * y;

			// Fill the depth array with the values of interest
			if (std::isfinite(depths[idx]) && depths[idx] > range_min &&
			    depths[idx] < range_max) {
				depth_array.push_back(depths[idx]);
        if (depths[idx] < min_value) {
          min_value = depths[idx];
          min_index = Point<double>(x,y);
        }
      }
		}
	}

	// compute minimum of all observations
	size_t n_depths = depth_array.size();
	if (n_depths > 0) {
		std::sort(depth_array.begin(), depth_array.end());
		return depth_array[0];
	}
	else
		return -1;
}
