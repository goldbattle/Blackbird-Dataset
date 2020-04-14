#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <opencv2/highgui/highgui.hpp>
#include <cv_bridge/cv_bridge.h>
#include <fstream>

class republishImages
{

  private:
  // Grab node handle to read the parameters
  ros::NodeHandle _nh;

  // Strings for input locations
  std::string _camera_name;
  std::string _camera_frame;
  std::string _input_folder;
  std::string _output_channel_name;
  std::string _ros_image_format;
  int _cv_image_format;

  bool _is_color;

  // Frame to grab the frame from the camera
  cv::Mat _frame;

  // Depth scale (meters)
  double _Z_far = 50.0;

  // Message Ptrs to populate the image messages
  sensor_msgs::ImagePtr _msg;

  // Video Capture ptr to grab the video streams
  std::unique_ptr<cv::VideoCapture> _capPtr;

  // Need an image transport ptr to publish images
  image_transport::ImageTransport _it;
  image_transport::Publisher _pub;

  // Subscribers for camera info subscribers
  ros::Subscriber _info_sub;
  ros::Publisher _info_pub;

  // Vector of all the timestamps
  std::vector<uint64_t> _timestamps;
  int _counter = 0;

  // Rate subsampling
  double _desiredRate = 360;
  double _lastTimestamp = 0;

public:
  republishImages(ros::NodeHandle nh, image_transport::ImageTransport it) : _nh(nh), _it(it)
  {
    _nh.getParam("camera_name", _camera_name);
    ROS_INFO("Camera name: %s", _camera_name.c_str());

    _nh.getParam("camera_frame", _camera_frame);
    ROS_INFO("Camera frame: %s", _camera_frame.c_str());


    _nh.getParam("input_folder", _input_folder);
    ROS_INFO("Input folder: %s", _input_folder.c_str());

    _nh.getParam("is_color", _is_color);
    ROS_INFO("Is color: %d", _is_color);

    // Figure out image types
    _ros_image_format = (_is_color) ? "8UC3" : "8UC1";
    _cv_image_format = (_is_color) ? CV_MAKETYPE(CV_8U, 3) : CV_MAKETYPE(CV_8U, 1);
    _output_channel_name = (_is_color) ? "image_rect_color" : "grayscale";

    std::stringstream timestamp_file_name;
    timestamp_file_name << _input_folder << "/" << _camera_name << "/nSecTimestamps.txt";
    
    std::stringstream video_file_name;
    video_file_name << _input_folder << "/" << _camera_name << "/lossless.mov";

    // Load timestamp file into memory
    uint64_t stamp_ns;
    std::ifstream infile(timestamp_file_name.str());
    while (infile >> stamp_ns)
    {
      _timestamps.push_back(stamp_ns);
    }

    _capPtr.reset(new cv::VideoCapture(video_file_name.str()));

    _pub = _it.advertise(_output_channel_name, 10);
    // _info_pub = _nh.advertise<sensor_msgs::CameraInfo>("camera_info", 10);

    _info_sub = _nh.subscribe("camera_info", 10, &republishImages::cb_camera, this);

    ROS_INFO("Finished reading timestamp file");
  };

  // Publishes an image every time it receives a camera info message
  void cb_camera(const sensor_msgs::CameraInfo::ConstPtr &msg)
  {
    // if ((msg->header.stamp.toSec() - lastTimestamp) < (1 / (_desiredRate + 1)))
    // {
    //   //ROS_INFO("Skipping frame");
    //   return;
    // }

    //ROS_INFO("Publishing Image");

    // Only publish an image if the camera_info message timestamp matches a timestamp in the .mov file.
    uint64_t camera_info_timestamp = ((uint64_t)(msg->header.stamp.sec * 1e9) + (uint64_t)msg->header.stamp.nsec);
    if (!std::binary_search(_timestamps.begin(), _timestamps.end(), camera_info_timestamp))
      return;
    while (_timestamps.at(_counter) != camera_info_timestamp)
    {
      *_capPtr >> _frame;
      _counter++;
      if (_counter >= _timestamps.size())
        throw std::runtime_error("Requested a timestamp not in the timestamps file");
    }

    // Handle image conversions
    if (!_frame.empty()){

      cv::Mat frame = cv::Mat(_frame.rows, _frame.cols, _cv_image_format);

      // Convert OpenCV mat to ROS message
      _msg = cv_bridge::CvImage(std_msgs::Header(), _ros_image_format, frame).toImageMsg();

      // Copy header from 
      _msg->header = msg->header;
      // Fix frame
      _msg->header.frame_id = _camera_frame;

      // Create camera info
      // sensor_msgs::CameraInfo cameraInfoMsg = *msg;
      // cameraInfoMsg.header.frame_id = _msg->header.frame_id;

      _pub.publish(_msg);
      // _info_pub.publish(cameraInfoMsg);
      _lastTimestamp = msg->header.stamp.toSec();
    }
  }


};

int main(int argc, char **argv)
{
  ros::init(argc, argv, "image_publisher");
  ros::NodeHandle nh("~");
  image_transport::ImageTransport it(nh);

  republishImages _republishImages(nh, it);

  ros::spin();
}


// Old Code for processing .mov files with depth images (not used)
// If is depth image, convert pixels from 32bit integers into depth.
// depth = raw_px * far_plane / 2^32
// cv::Mat _floatDepthImage = cv::Mat(_frame.rows, _frame.cols, CV_32FC1);
// float cx = msg->K[2];
// float cy = msg->K[5];
// float fx = msg->K[0];

// Iterate through pixels and remap them
// for (int i = 0; i < _frame.rows; i++)
// {
//   for (int j = 0; j < _frame.cols; j++)
//   {
//     float z_compressed = _frame.at<uint8_t>(i, j, 0) / (float)255;

//     // This is 0 at camera, 1 at z-far.
//     float eye_depth = ((std::pow(z_compressed, 4) * std::pow(_Z_far, 2)) + (_Z_far * _Z_near) + std::pow(_Z_near, 2)) / (_Z_far * (_Z_far + _Z_near));
//     //float eye_depth = std::pow(z_compressed,4);
//     // This depth is not the plane to plane depth, but is the depth along the ray.
//     float xy_dist_from_center = std::pow(std::pow(i - cy, 2) + std::pow(j - cx, 2), 0.5);
//     float theta = std::atan2(xy_dist_from_center, fx);

//     float ray_depth = (_Z_far / std::cos(theta)) * eye_depth;

//     //float true_depth = eye_depth*(_Z_far-_Z_near)+_Z_near;
//     // Convert to plane to plane
//     float plane_dist = ray_depth * std::cos(theta);

//     // You can now access the pixel value with cv::Vec3b
//     _floatDepthImage.at<float>(i, j) = plane_dist; //(_Z_near + std::pow(z_compressed, 4)*std::pow(_Z_far, 2)/(std::pow(255.0, 4)*(_Z_near + _Z_far)))*_C_scene;
//   }
// }