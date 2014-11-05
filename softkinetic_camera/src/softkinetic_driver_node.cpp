/*!
 *****************************************************************
 *
 *   Copyright (c) 2014
 *
 *   Fraunhofer Institute for Manufacturing Engineering
 *   and Automation (IPA)
 *
 *****************************************************************
 *
 *   ROS package name: softkinetic_camera
 *
 *   Author: Felipe Garcia Lopez, email: flg@ipa.fhg.de
 *
 *   Date of creation: March 2014
 *
 *****************************************************************
 *
 *  Software License Agreement (BSD License)
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *     - Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     - Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     - Neither the name of the Fraunhofer Institute for Manufacturing
 *       Engineering and Automation (IPA) nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 ****************************************************************/
// System include files
#include <stdio.h>
#include <signal.h>
#include <vector>
#include <exception>
#include <iostream>
#include <iomanip>
#include <fstream>
// ROS include files
#include <ros/ros.h>
#include <std_msgs/Int32.h>
#include <message_filters/subscriber.h>
#include <message_filters/time_synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>
// Pointcloud include files
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/point_cloud_conversion.h>
#include <pcl_ros/point_cloud.h>
#include <pcl_ros/io/pcd_io.h>
#include <pcl/io/io.h>
#include <pcl/point_types.h>
#include <pcl/range_image/range_image.h>
#include <pcl/filters/radius_outlier_removal.h>
#include <pcl/filters/voxel_grid.h>
// OpenCV + camera include files
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/imgproc/imgproc.hpp>
#include <sensor_msgs/image_encodings.h>
#include <camera_info_manager/camera_info_manager.h>
// Camera SDK include files
#include <DepthSense.hxx>

typedef struct _complete_color_config{
    DepthSense::ColorNode::Configuration base_config;
    DepthSense::ExposureAuto auto_exposure_mode;
    int32_t brightness;
    int32_t contrast;
    int32_t saturation;
    int32_t hue;
    int32_t gamma;
    int32_t white_balance;
    int32_t sharpness;
    int32_t exposure;
    bool enable_priority_auto_exposure_mode;
    bool enable_auto_white_balance;
    bool enable_color_map;
    bool enable_compressed_data;
} complete_color_config;

typedef struct _complete_depth_config{
    DepthSense::DepthNode::Configuration base_config;
    int32_t illumination_level;
    bool enable_xyz_depth;
    bool enable_floatingpoint_xyz_depth;
    bool enable_accelerometer;
    bool enable_confidence_map;
    bool enable_depth_map;
    bool enable_floatingpoint_depth_map;
    bool enable_phase_map;
    bool enable_uv_map;
    bool enable_vertices;
    bool enable_floatingpoint_vertices;
} complete_depth_config;

// DepthSense SDK global variables
DepthSense::Context g_context;
DepthSense::ColorNode g_color_node;
DepthSense::DepthNode g_depth_node;
complete_color_config g_color_config;
complete_depth_config g_depth_config;
// ROS global variables
ros::Publisher g_pointcloud_pub;
ros::Publisher g_rgb_pointcloud_pub;
image_transport::CameraPublisher g_rgb_pub;
image_transport::CameraPublisher g_depth_pub;
image_transport::CameraPublisher g_confidence_pub;
// Config global variables
std::string g_rgb_frame_name;
std::string g_rgb_optical_frame_name;
std::string g_depth_frame_name;
std::string g_depth_optical_frame_name;
sensor_msgs::CameraInfo g_rgb_camerainfo;
sensor_msgs::CameraInfo g_depth_camerainfo;
// Config for the confidence filter
int32_t g_confidence_threshold = 0;
// Save the current color map for coloring pointclouds
cv::Mat g_current_color_image;

void dealocate_pcl_pointcloud_fn(pcl::PointCloud<pcl::PointXYZ>* p)
{
}

void dealocate_pcl_rgb_pointcloud_fn(pcl::PointCloud<pcl::PointXYZRGB>* p)
{
}

void OnNewColorSample(DepthSense::ColorNode node, DepthSense::ColorNode::NewSampleReceivedData data)
{
    // Make new OpenCV container
    int32_t width = 0;
    int32_t height = 0;
    DepthSense::FrameFormat_toResolution(data.captureConfiguration.frameFormat, &width, &height);
    cv::Mat new_image_bgr8(height, width, CV_8UC3);
    // If the image is in YUY2 mode, we need to convert it to BGR8 first
    if (data.captureConfiguration.compression == DepthSense::COMPRESSION_TYPE_YUY2)
    {
        // Make the intermediate container for the YUY2 image
        cv::Mat new_image_yuy2(height, width, CV_8UC2);
        // Copy the data in
        new_image_yuy2.data = const_cast<u_int8_t*>(static_cast<const u_int8_t*>(data.colorMap));
        // Convert to BGR
        cv::cvtColor(new_image_yuy2, new_image_bgr8, CV_YUV2BGR_YUY2);
    }
    // If the image is in MJPEG mode, it's already in BGR8
    else
    {
        // Copy the data in
        new_image_bgr8.data = const_cast<u_int8_t*>(static_cast<const u_int8_t*>(data.colorMap));
    }
    // Save the current color image
    g_current_color_image = new_image_bgr8.clone();
    // Convert the OpenCV image to ROS
    std_msgs::Header new_image_header;
    new_image_header.frame_id = g_rgb_optical_frame_name;
    new_image_header.stamp = ros::Time::now();
    sensor_msgs::Image new_image;
    cv_bridge::CvImage new_image_converted(new_image_header, sensor_msgs::image_encodings::BGR8, new_image_bgr8);
    new_image_converted.toImageMsg(new_image);
    sensor_msgs::CameraInfo new_image_camerainfo = g_rgb_camerainfo;
    new_image_camerainfo.header = new_image_header;
    // Publish the image
    g_rgb_pub.publish(new_image, new_image_camerainfo);
}

inline bool is_vertex_valid(float x, float y, float z)
{
    if (x != -2.0 || y != -2.0 || z != -2.0)
    {
        return true;
    }
    else
    {
        return false;
    }
}

inline bool is_uv_valid(float u, float v)
{
    if (u != -FLT_MAX || v != -FLT_MAX)
    {
        return true;
    }
    else
    {
        return false;
    }
}

void OnNewDepthSample(DepthSense::DepthNode node, DepthSense::DepthNode::NewSampleReceivedData data)
{
    // First, we generate the raw depth image
    int32_t width = 0;
    int32_t height = 0;
    DepthSense::FrameFormat_toResolution(data.captureConfiguration.frameFormat, &width, &height);
    // Copy and build the depth image
    float* raw_depth_floats = const_cast<float*>(static_cast<const float*>(data.depthMapFloatingPoint));
    std::vector<float> depth_floats(raw_depth_floats, raw_depth_floats + (width * height));
    cv::Mat new_image_depth = cv::Mat(depth_floats).reshape(1, height);
    // Copy and build the confidences
    int16_t* raw_confidence_shorts = const_cast<int16_t*>(static_cast<const int16_t*>(data.confidenceMap));
    std::vector<int16_t> confidence_shorts(raw_confidence_shorts, raw_confidence_shorts + (width * height));
    cv::Mat new_image_confidences = cv::Mat(confidence_shorts).reshape(1, height);
    // Set depth image points with confidence below threshold to NaN
    ;
    // Filter the depth image
    cv::Mat new_image_depth_filtered(new_image_depth.rows, new_image_depth.cols, CV_32FC1);
    cv::Mat depth_mean_kernel(3, 3, CV_32FC1);
    depth_mean_kernel.setTo(1.0);
    cv::filter2D(new_image_depth, new_image_depth_filtered, CV_32FC1, depth_mean_kernel);
    // Convert the OpenCV depth image to ROS
    std_msgs::Header new_image_header;
    new_image_header.frame_id = g_depth_optical_frame_name;
    new_image_header.stamp = ros::Time::now();
    sensor_msgs::Image new_depth_image;
    cv_bridge::CvImage new_depth_image_converted(new_image_header, sensor_msgs::image_encodings::TYPE_32FC1, new_image_depth_filtered);
    new_depth_image_converted.toImageMsg(new_depth_image);
    sensor_msgs::CameraInfo new_depth_image_camerainfo = g_depth_camerainfo;
    new_depth_image_camerainfo.header = new_image_header;
    // Publish the image
    g_depth_pub.publish(new_depth_image, new_depth_image_camerainfo);
//    // Convert the OpenCV confidence image to ROS
//    sensor_msgs::Image new_confidence_image;
//    cv_bridge::CvImage new_confidence_image_converted(new_image_header, sensor_msgs::image_encodings::TYPE_16SC1, new_image_confidences);
//    new_confidence_image_converted.toImageMsg(new_confidence_image);
//    sensor_msgs::CameraInfo new_confidence_image_camerainfo = g_depth_camerainfo;
//    new_confidence_image_camerainfo.header = new_image_header;
//    // Publish the image
//    g_confidence_pub.publish(new_confidence_image, new_confidence_image_camerainfo);
    // Second, generate the pointcloud
    DepthSense::FPVertex* raw_vertices = const_cast<DepthSense::FPVertex*>(static_cast<const DepthSense::FPVertex*>(data.verticesFloatingPoint));
    std::vector<DepthSense::FPVertex> vertices(raw_vertices, raw_vertices + (width * height));
    // Make the XYZ pointcloud
    pcl::PointCloud<pcl::PointXYZ> pcl_pointcloud;
    for (size_t idx = 0; idx < vertices.size(); idx++)
    {
        float x = vertices[idx].x;
        float y = -vertices[idx].y;
        float z = vertices[idx].z;
        if (is_vertex_valid(x, y, z))
        {
            // Filter based on confidence
            int32_t confidence = (int32_t)confidence_shorts[idx];
            if (confidence >= g_confidence_threshold)
            {
                pcl::PointXYZ new_point(x, y, z);
                pcl_pointcloud.push_back(new_point);
            }
        }
    }
    // Publish the cloud without filtering
    // Convert the XYZ pointcloud to ROS message
    sensor_msgs::PointCloud2 ros_pointcloud;
    pcl::toROSMsg(pcl_pointcloud, ros_pointcloud);
    ros_pointcloud.header.frame_id = g_depth_optical_frame_name;
    ros_pointcloud.header.stamp = new_image_header.stamp;
    g_pointcloud_pub.publish(ros_pointcloud);
    // Make the XYZRGB pointcloud (if enabled)
    if (g_color_config.enable_color_map && !g_current_color_image.empty())
    {
        // Get the UV map that maps the color map and vertices together
        DepthSense::UV* raw_uv = const_cast<DepthSense::UV*>(static_cast<const DepthSense::UV*>(data.uvMap));
        std::vector<DepthSense::UV> uv(raw_uv, raw_uv + (width * height));
        // Make the XYZRGB pointcloud
        pcl::PointCloud<pcl::PointXYZRGB> pcl_rgb_pointcloud;
        for (size_t idx = 0; idx < vertices.size(); idx++)
        {
            float x = vertices[idx].x;
            float y = -vertices[idx].y;
            float z = vertices[idx].z;
            float u = uv[idx].u;
            float v = uv[idx].v;
            if (is_vertex_valid(x, y, z) && is_uv_valid(u, v))
            {
                // Lookup RGB colors
                size_t image_height = (size_t)(v * g_current_color_image.rows);
                size_t image_width = (size_t)(u * g_current_color_image.cols);
                cv::Vec3b pixel = g_current_color_image.at<cv::Vec3b>(image_height, image_width);
                uint8_t blue = pixel[0];
                uint8_t green = pixel[1];
                uint8_t red = pixel[2];
                // Filter based on confidence
                int32_t confidence = (int32_t)confidence_shorts[idx];
                if (confidence >= g_confidence_threshold)
                {
                    // Make the point
                    pcl::PointXYZRGB new_point(red, green, blue);
                    new_point.data[0] = x;
                    new_point.data[1] = y;
                    new_point.data[2] = z;
                    pcl_rgb_pointcloud.push_back(new_point);
                }
            }
        }
        // Publish the cloud without filtering
        // Convert the XYZRGB pointcloud to ROS message
        sensor_msgs::PointCloud2 ros_rgb_pointcloud;
        pcl::toROSMsg(pcl_rgb_pointcloud, ros_rgb_pointcloud);
        ros_rgb_pointcloud.header.frame_id = g_depth_optical_frame_name;
        ros_rgb_pointcloud.header.stamp = new_image_header.stamp;
        g_rgb_pointcloud_pub.publish(ros_rgb_pointcloud);
    }
    else if (g_color_config.enable_color_map && g_current_color_image.empty())
    {
        ROS_WARN("XYZRGB pointclouds enabled, but no color image received yet. Not publishing XYZRGB pointcloud");
    }
}

void setupCameraInfo(const DepthSense::IntrinsicParameters& params, sensor_msgs::CameraInfo& cam_info)
{
  cam_info.distortion_model = "plumb_bob";
  cam_info.height = params.height;
  cam_info.width  = params.width;

  // Distortion parameters D = [k1, k2, t1, t2, k3]
  cam_info.D.resize(5);
  cam_info.D[0] = params.k1;
  cam_info.D[1] = params.k2;
  cam_info.D[2] = params.p1;
  cam_info.D[3] = params.p2;
  cam_info.D[4] = params.k3;

  // Intrinsic camera matrix for the raw (distorted) images:
  //     [fx  0 cx]
  // K = [ 0 fy cy]
  //     [ 0  0  1]
  cam_info.K[0] = params.fx;
  cam_info.K[2] = params.cx;
  cam_info.K[4] = params.fy;
  cam_info.K[5] = params.cy;
  cam_info.K[8] = 1.0;

  // Rectification matrix (stereo cameras only)
  //     [1 0 0]
  // R = [0 1 0]
  //     [0 0 1]
  cam_info.R[0] = 1.0;
  cam_info.R[4] = 1.0;
  cam_info.R[8] = 1.0;

  // Projection/camera matrix; we use the same values as in the raw image, as we are not
  // applying any correction (WARN: is this ok?). For monocular cameras, Tx = Ty = 0.
  //     [fx'  0  cx' Tx]
  // P = [ 0  fy' cy' Ty]
  //     [ 0   0   1   0]
  cam_info.P[0] = params.fx;
  cam_info.P[2] = params.cx;
  cam_info.P[5] = params.fy;
  cam_info.P[6] = params.cy;
  cam_info.P[10] = 1.0;
}

void ConfigureDepthNode(DepthSense::DepthNode& depth_node)
{
    ROS_INFO("Configuring new DepthNode...");
    // Register the event handler
    depth_node.newSampleReceivedEvent().connect(&OnNewDepthSample);
    // Try to configure the node
    try
    {
        g_context.requestControl(depth_node, 0);
        depth_node.setConfiguration(g_depth_config.base_config);
        depth_node.setDepthMap3Planes(g_depth_config.enable_xyz_depth);
        depth_node.setDepthMapFloatingPoint3Planes(g_depth_config.enable_floatingpoint_xyz_depth);
        depth_node.setEnableAccelerometer(g_depth_config.enable_accelerometer);
        depth_node.setEnableConfidenceMap(g_depth_config.enable_confidence_map);
        depth_node.setEnableDepthMap(g_depth_config.enable_depth_map);
        depth_node.setEnableDepthMapFloatingPoint(g_depth_config.enable_floatingpoint_depth_map);
        depth_node.setEnablePhaseMap(g_depth_config.enable_phase_map);
        depth_node.setEnableUvMap(g_depth_config.enable_uv_map);
        depth_node.setEnableVertices(g_depth_config.enable_vertices);
        depth_node.setEnableVerticesFloatingPoint(g_depth_config.enable_floatingpoint_vertices);
        if (depth_node.illuminationLevelIsReadOnly())
        {
            ROS_WARN("Illumination level cannot be set on this camera");
        }
        else
        {
            depth_node.setIlluminationLevel(g_depth_config.illumination_level);
        }
        ROS_INFO("...new DepthNode configuration applied");
    }
    catch (DepthSense::ArgumentException& e)
    {
        ROS_ERROR("Argument exception [%s] caught when attempting to configure depth node", e.what());
    }
    catch (DepthSense::UnauthorizedAccessException& e)
    {
        ROS_ERROR("Unauthorized access exception [%s] caught when attempting to configure depth node", e.what());
    }
    catch (DepthSense::IOException& e)
    {
        ROS_ERROR("IO exception [%s] caught when attempting to configure depth node", e.what());
    }
    catch (DepthSense::InvalidOperationException& e)
    {
        ROS_ERROR("Invalid operation exception [%s] caught when attempting to configure depth node", e.what());
    }
    catch (DepthSense::ConfigurationException& e)
    {
        ROS_ERROR("Configuration exception [%s] caught when attempting to configure depth node", e.what());
    }
    catch (DepthSense::StreamingException& e)
    {
        ROS_ERROR("Streaming exception [%s] caught when attempting to configure depth node", e.what());
    }
    catch (DepthSense::TimeoutException& e)
    {
        ROS_ERROR("Timeout exception [%s] caught when attempting to configure depth node", e.what());
    }
    catch (DepthSense::Exception& e)
    {
        ROS_ERROR("Unplanned exception [%s] caught when attempting to configure depth node", e.what());
    }
}

void ConfigureColorNode(DepthSense::ColorNode& color_node)
{
    ROS_INFO("Configuring new ColorNode...");
    // Register the event handler
    color_node.newSampleReceivedEvent().connect(&OnNewColorSample);
    // Try to configure the node
    try
    {
        g_context.requestControl(color_node, 0);
        color_node.setConfiguration(g_color_config.base_config);
        color_node.setBrightness(g_color_config.brightness);
        color_node.setContrast(g_color_config.contrast);
        color_node.setEnableColorMap(g_color_config.enable_color_map);
        color_node.setEnableCompressedData(g_color_config.enable_compressed_data);
        color_node.setGamma(g_color_config.gamma);
        color_node.setHue(g_color_config.hue);
        color_node.setSaturation(g_color_config.saturation);
        color_node.setSharpness(g_color_config.sharpness);
        // Set the exposure
        if (color_node.exposureAutoIsReadOnly() || color_node.exposureAutoPriorityIsReadOnly() || color_node.exposureIsReadOnly())
        {
            ROS_WARN("Exposure cannot be set on this camera");
        }
        else
        {
            if (g_color_config.enable_priority_auto_exposure_mode)
            {
                color_node.setExposureAuto(DepthSense::EXPOSURE_AUTO_APERTURE_PRIORITY);
                color_node.setExposureAutoPriority(true);
            }
            else
            {
                color_node.setExposureAuto(DepthSense::EXPOSURE_AUTO_MANUAL);
                color_node.setExposureAutoPriority(false);
                color_node.setExposure(g_color_config.exposure);
            }
        }
        // Set the white balance
        if (g_color_config.enable_auto_white_balance)
        {
            color_node.setWhiteBalanceAuto(true);
        }
        else
        {
            color_node.setWhiteBalanceAuto(false);
            color_node.setWhiteBalance(g_color_config.white_balance);
        }
        ROS_INFO("...new ColorNode configuration applied");
    }
    catch (DepthSense::ArgumentException& e)
    {
        ROS_ERROR("Argument exception [%s] caught when attempting to configure color node", e.what());
    }
    catch (DepthSense::UnauthorizedAccessException& e)
    {
        ROS_ERROR("Unauthorized access exception [%s] caught when attempting to configure color node", e.what());
    }
    catch (DepthSense::IOException& e)
    {
        ROS_ERROR("IO exception [%s] caught when attempting to configure color node", e.what());
    }
    catch (DepthSense::InvalidOperationException& e)
    {
        ROS_ERROR("Invalid operation exception [%s] caught when attempting to configure color node", e.what());
    }
    catch (DepthSense::ConfigurationException& e)
    {
        ROS_ERROR("Configuration exception [%s] caught when attempting to configure color node", e.what());
    }
    catch (DepthSense::StreamingException& e)
    {
        ROS_ERROR("Streaming exception [%s] caught when attempting to configure color node", e.what());
    }
    catch (DepthSense::TimeoutException& e)
    {
        ROS_ERROR("Timeout exception [%s] caught when attempting to configure color node", e.what());
    }
    catch (DepthSense::Exception& e)
    {
        ROS_ERROR("Unplanned exception [%s] caught when attempting to configure color node", e.what());
    }
}

void ConfigureNode(DepthSense::Node node)
{
    if ((node.is<DepthSense::DepthNode>()) && (!g_depth_node.isSet()))
    {
        g_depth_node = node.as<DepthSense::DepthNode>();
        ConfigureDepthNode(g_depth_node);
        g_context.registerNode(node);
    }
    if ((node.is<DepthSense::ColorNode>()) && (!g_color_node.isSet()))
    {
        g_color_node = node.as<DepthSense::ColorNode>();
        ConfigureColorNode(g_color_node);
        g_context.registerNode(node);
    }
}

void OnNodeConnected(DepthSense::Device device, DepthSense::Device::NodeAddedData data)
{
    ConfigureNode(data.node);
    ROS_INFO("Node connected");
}

void OnNodeRemoved(DepthSense::Device device, DepthSense::Device::NodeRemovedData data)
{
    if (data.node.is<DepthSense::ColorNode>() && (data.node.as<DepthSense::ColorNode>() == g_color_node))
    {
        g_color_node.unset();
    }
    if (data.node.is<DepthSense::DepthNode>() && (data.node.as<DepthSense::DepthNode>() == g_depth_node))
    {
        g_depth_node.unset();
    }
    ROS_WARN("Node removed");
}

void OnDeviceConnected(DepthSense::Context context, DepthSense::Context::DeviceAddedData data)
{
    ROS_INFO("Device connected");
}

void OnDeviceRemoved(DepthSense::Context context, DepthSense::Context::DeviceRemovedData data)
{
    ROS_ERROR("Device removed");
}

void shutdown(int signum)
{
    if (signum == SIGINT)
    {
        ROS_WARN("Attempting to shutdown SoftKinetic camera driver node...");
        if (g_context.isSet())
        {
            ROS_WARN("...shutting down SDK context");
            g_context.quit();
            g_context.stopNodes();
            if (g_color_node.isSet())
            {
                g_context.unregisterNode(g_color_node);
            }
            if (g_depth_node.isSet())
            {
                g_context.unregisterNode(g_depth_node);
            }
            g_context.unset();
        }
        ROS_WARN("...shutting down ROS");
        ros::shutdown();
        printf("...exiting!\n");
        fflush(stdout);
    }
}

int main(int argc, char** argv)
{
    // Initialize ROS - we have our own SIGINT handler, so we tell ROS not to provide one
    ros::init(argc, argv, "softkinetic_driver_node", ros::init_options::NoSigintHandler);
    // Register our own SIGINT handler
    signal(SIGINT, shutdown);
    // Public nodehandle
    ros::NodeHandle nh;
    // Private nodehandle (for parameter calls)
    ros::NodeHandle nhp("~");
    // Image transport handler (for image compression/decompression)
    image_transport::ImageTransport it(nh);
    ////////////////////////////////////////////////////////////////////////////////
    // Load parameters from the param server
    // TF frame prefix for the whole camera
    std::string camera_name;
    nhp.param(std::string("camera_name"), camera_name, std::string("softkinetic_camera"));
    // TF frame for the whole camera
    std::string camera_frame = camera_name + "_frame";
    // TF frame(s) for the RGB camera
    g_rgb_frame_name = camera_name + "_rgb_frame";
    g_rgb_optical_frame_name = camera_name + "_rgb_optical_frame";
    // TF frame(s) for the depth camera
    g_depth_frame_name = camera_name + "_depth_frame";
    g_depth_optical_frame_name = camera_name + "_depth_optical_frame";
    ////////////////////////////////////////////////////////////////////////////////
    /////       Get the configuration parameters for confidence filtering      /////
    ////////////////////////////////////////////////////////////////////////////////
    nhp.param(std::string("confidence_threshold"), g_confidence_threshold, 100);
    ////////////////////////////////////////////////////////////////////////////////
    /////       Get the configuration parameters for the color camera          /////
    ////////////////////////////////////////////////////////////////////////////////
    // Get the resolution of the RGB camera
    std::string rgb_resolution_string;
    nhp.param(std::string("rgb_resolution"), rgb_resolution_string, std::string("VGA"));
    // Sanity check the RGB resolution
    DepthSense::FrameFormat rgb_resolution = DepthSense::FRAME_FORMAT_VGA;
    if (rgb_resolution_string == "QQVGA" || rgb_resolution_string == "qqvga")
    {
        rgb_resolution = DepthSense::FRAME_FORMAT_QQVGA;
    }
    else if (rgb_resolution_string == "QVGA" || rgb_resolution_string == "qvga")
    {
        rgb_resolution = DepthSense::FRAME_FORMAT_QVGA;
    }
    else if (rgb_resolution_string == "VGA" || rgb_resolution_string == "vga")
    {
        rgb_resolution = DepthSense::FRAME_FORMAT_VGA;
    }
    else if (rgb_resolution_string == "NHD" || rgb_resolution_string == "nhd")
    {
        rgb_resolution = DepthSense::FRAME_FORMAT_NHD;
    }
    else if (rgb_resolution_string == "WXGA_H" || rgb_resolution_string == "wxga_h")
    {
        rgb_resolution = DepthSense::FRAME_FORMAT_WXGA_H;
    }
    else
    {
        ROS_FATAL("Invalid RGB resolution %s", rgb_resolution_string.c_str());
        shutdown(SIGINT);
    }
    // Get the target RGB framerate (0 means RGB camera is disabled)
    int32_t rgb_framerate = 30;
    nhp.param(std::string("rgb_framerate"), rgb_framerate, 30);
    if (rgb_framerate < 0)
    {
        ROS_FATAL("RGB framerate cannot be less than 0");
        shutdown(SIGINT);
    }
    if (rgb_framerate == 0)
    {
        ROS_WARN("RGB camera will be disabled");
    }
    // Get the compression mode of the RGB camera
    std::string rgb_compression_string;
    nhp.param(std::string("rgb_compression_type"), rgb_compression_string, std::string("MJPEG"));
    // Sanity check the RGB compression mode
    DepthSense::CompressionType rgb_compression_type = DepthSense::COMPRESSION_TYPE_MJPEG;
    if (rgb_compression_string == "MJPEG" || rgb_compression_string == "mjpeg")
    {
        rgb_compression_type = DepthSense::COMPRESSION_TYPE_MJPEG;
    }
    else if (rgb_compression_string == "YUY2" || rgb_compression_string == "yuy2")
    {
        rgb_compression_type = DepthSense::COMPRESSION_TYPE_YUY2;
    }
    else
    {
        ROS_FATAL("Invalid RGB compression mode %s", rgb_compression_string.c_str());
        shutdown(SIGINT);
    }
    // Get the powerline frequency (0 is disabled)
    int32_t powerline_hertz = 60;
    nhp.param(std::string("powerline_frequency"), powerline_hertz, 60);
    // Sanity check the powerline frequency
    DepthSense::PowerLineFrequency powerline_frequency = DepthSense::POWER_LINE_FREQUENCY_60HZ;
    if (powerline_hertz == 60)
    {
        powerline_frequency = DepthSense::POWER_LINE_FREQUENCY_60HZ;
    }
    else if (powerline_hertz == 50)
    {
        powerline_frequency = DepthSense::POWER_LINE_FREQUENCY_50HZ;
    }
    else if (powerline_hertz == 0)
    {
        powerline_frequency = DepthSense::POWER_LINE_FREQUENCY_DISABLED;
        ROS_WARN("Powerline frequency disabled");
    }
    else
    {
        ROS_FATAL("Invalid powerline frequency %d Hz", powerline_hertz);
        shutdown(SIGINT);
    }

    // Get the brightness of the RGB camera
    int32_t rgb_brightness = 0;
    nhp.param(std::string("rgb_brightness"), rgb_brightness, 0);
    // Get the contrast of the RGB camera
    int32_t rgb_contrast = 5;
    nhp.param(std::string("rgb_contrast"), rgb_contrast, 5);
    // Get the saturation of the RGB camera
    int32_t rgb_saturation = 5;
    nhp.param(std::string("rgb_saturation"), rgb_saturation, 5);
    // Get the hue of the RGB camera
    int32_t rgb_hue = 0;
    nhp.param(std::string("rgb_hue"), rgb_hue, 0);
    // Get the gamma of the RGB camera
    int32_t rgb_gamma = 3;
    nhp.param(std::string("rgb_gamma"), rgb_gamma, 3);
    // Get the sharpness of the RGB camera
    int32_t rgb_sharpness = 5;
    nhp.param(std::string("rgb_sharpness"), rgb_sharpness, 5);
    // Get the exposure of the RGB camera (0 means autoexposure)
    int32_t rgb_exposure = 0;
    nhp.param(std::string("rgb_exposure"), rgb_exposure, 0);
    // Get the white balance of the RGB camera (0 means automatic white balance)
    int32_t rgb_white_balance = 4650;
    nhp.param(std::string("rgb_white_balance"), rgb_white_balance, 4650);

    // Get the enable/disable for compressed data from the camera
    bool enable_rgb_compression = false;
    nhp.param(std::string("enable_rgb_compression"), enable_rgb_compression, false);

    ////////////////////////////////////////////////////////////////////////////////
    /////       Set the configuration parameters for the color camera          /////
    ////////////////////////////////////////////////////////////////////////////////
    // Set the basic parameters
    g_color_config.base_config.compression = rgb_compression_type;
    g_color_config.base_config.frameFormat = rgb_resolution;
    g_color_config.base_config.framerate = rgb_framerate;
    g_color_config.base_config.powerLineFrequency = powerline_frequency;
    // Set the rgb camera enabled/disabled
    if (rgb_framerate > 0)
    {
        g_color_config.enable_color_map = true;
    }
    else
    {
        g_color_config.enable_color_map = false;
    }
    // Set the rest of the parameters
    g_color_config.brightness = rgb_brightness;
    g_color_config.contrast = rgb_contrast;
    g_color_config.saturation = rgb_saturation;
    g_color_config.hue = rgb_hue;
    g_color_config.gamma = rgb_gamma;
    g_color_config.sharpness = rgb_sharpness;
    g_color_config.enable_compressed_data = enable_rgb_compression;
    // Set the exposure controls
    if (rgb_exposure == 0)
    {
        g_color_config.enable_priority_auto_exposure_mode = true;
        g_color_config.auto_exposure_mode = DepthSense::EXPOSURE_AUTO_APERTURE_PRIORITY;
    }
    else
    {
        g_color_config.enable_priority_auto_exposure_mode = false;
        g_color_config.auto_exposure_mode = DepthSense::EXPOSURE_AUTO_MANUAL;
        g_color_config.exposure = rgb_exposure;
    }
    // Set the white balance controls
    if (rgb_white_balance == 0)
    {
        g_color_config.enable_auto_white_balance = true;
    }
    else
    {
        g_color_config.enable_auto_white_balance = false;
        g_color_config.white_balance = rgb_white_balance;
    }
    // Get the RGB calibration filepath
    std::string rgb_calibration_file;
    nhp.param(std::string("rgb_calibration_file"), rgb_calibration_file, std::string(""));
    if (rgb_calibration_file == "")
    {
        ROS_WARN("No calibration provided for RGB camera");
    }
    ////////////////////////////////////////////////////////////////////////////////
    /////       Get the configuration parameters for the depth camera          /////
    ////////////////////////////////////////////////////////////////////////////////
    // Get the resolution of the depth camera
    std::string depth_resolution_string;
    nhp.param(std::string("depth_resolution"), depth_resolution_string, std::string("QVGA"));
    // Sanity check the depth resolution
    DepthSense::FrameFormat depth_resolution = DepthSense::FRAME_FORMAT_QVGA;
    if (depth_resolution_string == "QQVGA" || depth_resolution_string == "qqvga")
    {
        depth_resolution = DepthSense::FRAME_FORMAT_QQVGA;
    }
    else if (depth_resolution_string == "QVGA" || depth_resolution_string == "qvga")
    {
        depth_resolution = DepthSense::FRAME_FORMAT_QVGA;
    }
    else if (depth_resolution_string == "VGA" || depth_resolution_string == "vga")
    {
        depth_resolution = DepthSense::FRAME_FORMAT_VGA;
    }
    else
    {
        ROS_FATAL("Invalid depth resolution %s", depth_resolution_string.c_str());
        shutdown(SIGINT);
    }
    // Get the target depth framerate (0 means depth camera is disabled)
    int32_t depth_framerate = 30;
    nhp.param(std::string("depth_framerate"), depth_framerate, 30);
    if (depth_framerate < 0)
    {
        ROS_FATAL("Color framerate cannot be less than 0");
        shutdown(SIGINT);
    }
    if (depth_framerate == 0)
    {
        ROS_WARN("Depth camera will be disabled");
    }
    // Get the operating mode of the depth camera
    std::string depth_camera_mode_string;
    nhp.param(std::string("depth_camera_mode"), depth_camera_mode_string, std::string("CLOSE"));
    // Sanity check the color compression mode
    DepthSense::DepthNode::CameraMode depth_camera_mode = DepthSense::DepthNode::CAMERA_MODE_LONG_RANGE;
    if (depth_camera_mode_string == "LONG" || depth_camera_mode_string == "long")
    {
        depth_camera_mode = DepthSense::DepthNode::CAMERA_MODE_LONG_RANGE;
    }
    else if (depth_camera_mode_string == "CLOSE" || depth_camera_mode_string == "close")
    {
        depth_camera_mode = DepthSense::DepthNode::CAMERA_MODE_CLOSE_MODE;
    }
    else
    {
        ROS_FATAL("Invalid depth camera mode %s", depth_camera_mode_string.c_str());
        shutdown(SIGINT);
    }
    // Get the saturation setting for the depth camera
    bool depth_saturation = false;
    nhp.param(std::string("depth_saturation"), depth_saturation, false);
    // Get the illumination level for the depth camera
    int32_t depth_illumination_level = 100;
    nhp.param(std::string("depth_illumination_level"), depth_illumination_level, 100);
    ////////////////////////////////////////////////////////////////////////////////
    /////       Set the configuration parameters for the depth camera          /////
    ////////////////////////////////////////////////////////////////////////////////
    // Set the basic parameters
    g_depth_config.base_config.frameFormat = depth_resolution;
    g_depth_config.base_config.framerate = depth_framerate;
    g_depth_config.base_config.mode = depth_camera_mode;
    g_depth_config.base_config.saturation = depth_saturation;
    // Set the depth camera enabled/disabled
    if (depth_framerate > 0)
    {
        g_depth_config.enable_floatingpoint_vertices = true;
        g_depth_config.enable_uv_map = true;
        g_depth_config.enable_xyz_depth = false;
        g_depth_config.enable_floatingpoint_xyz_depth = false;
        g_depth_config.enable_accelerometer = false;
        g_depth_config.enable_confidence_map = true;
        g_depth_config.enable_depth_map = false;
        g_depth_config.enable_floatingpoint_depth_map = true;
        g_depth_config.enable_phase_map = false;
        g_depth_config.enable_vertices = false;
    }
    else
    {
        g_depth_config.enable_floatingpoint_vertices = false;
        g_depth_config.enable_uv_map = false;
        g_depth_config.enable_xyz_depth = false;
        g_depth_config.enable_floatingpoint_xyz_depth = false;
        g_depth_config.enable_accelerometer = false;
        g_depth_config.enable_confidence_map = false;
        g_depth_config.enable_depth_map = false;
        g_depth_config.enable_floatingpoint_depth_map = false;
        g_depth_config.enable_phase_map = false;
        g_depth_config.enable_vertices = false;
    }
    // Set the rest of the parameters
    g_depth_config.illumination_level = depth_illumination_level;
    // Get the depth calibration filepath
    std::string depth_calibration_file;
    nhp.param(std::string("depth_calibration_file"), depth_calibration_file, std::string(""));
    if (depth_calibration_file == "")
    {
        ROS_WARN("No calibration provided for depth camera");
    }
    ////////////////////////////////////////////////////////////////////////////////
    // Get the device index (to select between multiple cameras)
    int32_t device_index = 0;
    nhp.param(std::string("device_index"), device_index, 0);
    if (device_index < 0)
    {
        ROS_FATAL("Device index cannot be less than 0");
        shutdown(SIGINT);
    }
    ////////////////////////////////////////////////////////////////////////////////
    // Initialize publishers
    g_pointcloud_pub = nh.advertise<sensor_msgs::PointCloud2>(camera_name + "/points_xyz", 1);
    g_rgb_pointcloud_pub = nh.advertise<sensor_msgs::PointCloud2>(camera_frame + "/points_xyzrgb", 1);
    g_rgb_pub = it.advertiseCamera(camera_name + "/color", 1);
    g_depth_pub = it.advertiseCamera(camera_name + "/depth", 1);
    g_confidence_pub = it.advertiseCamera(camera_name + "/depth_confidence", 1);
    ////////////////////////////////////////////////////////////////////////////////
    // Initialize the SDK
    g_context = DepthSense::Context::create();
    g_context.deviceAddedEvent().connect(&OnDeviceConnected);
    g_context.deviceRemovedEvent().connect(&OnDeviceRemoved);
    ////////////////////////////////////////////////////////////////////////////////
    // Get the currently available devices
    std::vector<DepthSense::Device> devices = g_context.getDevices();
    if (devices.size() == 0)
    {
        ROS_FATAL("No SoftKinetic camera connected");
        shutdown(SIGINT);
    }
    if (!(device_index < devices.size()))
    {
        ROS_FATAL("Device index %d is invalid for %zu devices", device_index, devices.size());
        shutdown(SIGINT);
    }
    ////////////////////////////////////////////////////////////////////////////////
    // Get the desired device
    DepthSense::Device& device = devices[device_index];
    // Configure the device
    device.nodeAddedEvent().connect(&OnNodeConnected);
    device.nodeRemovedEvent().connect(&OnNodeRemoved);
    // Get the nodes of the device
    std::vector<DepthSense::Node> device_nodes = device.getNodes();
    // Configure the nodes
    for (size_t idx = 0; idx < device_nodes.size(); idx++)
    {
        ConfigureNode(device_nodes[idx]);
    }
    ////////////////////////////////////////////////////////////////////////////////
    // Run until stopped
    g_context.startNodes();
    g_context.run();
    // If stopped, call the cleanup code
    ROS_INFO("Calling shutdown...");
    shutdown(SIGINT);
    return 0;
}
