#include<sq_fitting/sq_fitter.h>
#include<tf/transform_listener.h>
#include <Eigen/Eigen>
#include <eigen_conversions/eigen_msg.h>
#include <tf_conversions/tf_eigen.h>



SQFitter::SQFitter(ros::NodeHandle &node, const std::string &cloud_topic, const std::string &output_frame, const SQFitter::Parameters &params)
  : table_plane_cloud_(new PointCloud), segmented_objects_cloud_(new PointCloud), objects_on_table_(new PointCloud),
  cloud_(new PointCloud), filtered_cloud_(new PointCloud), cut_cloud_(new PointCloud), output_frame_(output_frame)
{
  cloud_sub_ = node.subscribe(cloud_topic, 1, &SQFitter::cloud_callback, this);
  table_pub_ = node.advertise<sensor_msgs::PointCloud2>("table",10);
  objects_on_table_pub_ = node.advertise<sensor_msgs::PointCloud2>("tabletop_objects",10);
  objects_pub_ = node.advertise<sensor_msgs::PointCloud2>("segmented_objects",10);
  superquadrics_pub_ = node.advertise<sensor_msgs::PointCloud2>("superquadrics",10);
  filtered_cloud_pub_ = node.advertise<sensor_msgs::PointCloud2>("filtered_cloud",10);
  poses_pub_ = node.advertise<geometry_msgs::PoseArray>("sq_poses",10);
  cut_cloud_pub_ = node.advertise<sensor_msgs::PointCloud2>("cut_cloud", 10);
  sqs_pub_ = node.advertise<sq_fitting::sqArray>("sqs",10);

  this->sq_param_ = params;
  this->seg_param_ = params.seg_params;
  this->initialized = true;
  Objects_.resize(0);
}

void SQFitter::cloud_callback(const sensor_msgs::PointCloud2ConstPtr &input)
{
  pcl::fromROSMsg(*input, *cloud_);
  this->input_msg_ = *input;
  filterCloud(this->cloud_, this->filtered_cloud_);
  getSegmentedObjects(this->filtered_cloud_);
  getSuperquadricParameters(this->params_);
  sampleSuperquadrics(this->params_);
}

void SQFitter::transformFrame(const geometry_msgs::Pose &pose_in, geometry_msgs::Pose& pose_out)
{
  //std::cout<<"Output_frame: "<<output_frame_<<std::endl;
  //std::cout<<"Input frame: "<<this->input_msg_.header.frame_id<<std::endl;
  if(output_frame_ == this->input_msg_.header.frame_id)
  {
    std::cout<<"Ok till now"<<std::endl;
    pose_out = pose_in;
  }
  else{

  tf::TransformListener listener;
  tf::StampedTransform transform;
  try{

    listener.waitForTransform( output_frame_,this->input_msg_.header.frame_id,ros::Time(0), ros::Duration(3.0));
    listener.lookupTransform(output_frame_, this->input_msg_.header.frame_id,ros::Time(0), transform);
    //std::cout<<"Transform: "<<transform.getOrigin().x()<<" "<<transform.getOrigin().y()<<" "<<transform.getOrigin().z()<<std::endl;

    geometry_msgs::Pose inter_pose;
    inter_pose.position.x = transform.getOrigin().x();
    inter_pose.position.y = transform.getOrigin().y();
    inter_pose.position.z = transform.getOrigin().z();
    inter_pose.orientation.x = transform.getRotation().getX();
    inter_pose.orientation.y = transform.getRotation().getY();
    inter_pose.orientation.z = transform.getRotation().getZ();
    inter_pose.orientation.w = transform.getRotation().getW();
    Eigen::Affine3d transform_in_eigen;
    tf::poseMsgToEigen(inter_pose, transform_in_eigen);

    Eigen::Affine3f pose_in_eigen;
    sq_create_transform(pose_in, pose_in_eigen);

    tf::poseEigenToMsg( transform_in_eigen * pose_in_eigen.cast<double>(), pose_out );


  }
  catch (tf::TransformException ex){
        ROS_ERROR("%s",ex.what());
        ros::Duration(1.0).sleep();
      }

  }
}

void SQFitter::filterCloud(const CloudPtr& cloud, CloudPtr& filtered_cloud)
{
  CloudPtr cloud_nan(new PointCloud);
  for(size_t i=0;i<cloud->points.size();++i)
  {
    if(cloud->points[i].x > sq_param_.ws_limits[0] && cloud->points[i].x < sq_param_.ws_limits[1])
    {
      if(cloud->points[i].y > sq_param_.ws_limits[2] && cloud->points[i].y < sq_param_.ws_limits[3])
      {
        if(cloud->points[i].z > sq_param_.ws_limits[4] && cloud->points[i].z < sq_param_.ws_limits[5])
        {
          cloud_nan->points.push_back(cloud_->points[i]);
        }
      }
    }
  }
  cloud_nan->height = 1;
  cloud_nan->header.frame_id = cloud->header.frame_id;
  cloud_nan->width = cloud_nan->points.size();
  std::vector<int> indices;
  pcl::removeNaNFromPointCloud(*cloud_nan, *filtered_cloud, indices);
  pcl::toROSMsg(*filtered_cloud_, filtered_cloud_ros_);
  filtered_cloud_ros_.header.seq = 1;
  filtered_cloud_ros_.header.frame_id = this->input_msg_.header.frame_id;
  filtered_cloud_ros_.header.stamp = ros::Time::now();

}

void SQFitter::getSegmentedObjects(CloudPtr& cloud)
{
  Segmentation* seg = new Segmentation(cloud, this->seg_param_);
  seg->segment();

  seg->getTablecloud(table_plane_cloud_);
  pcl::toROSMsg(*table_plane_cloud_, table_cloud_);
  table_cloud_.header.seq = 1;
  table_cloud_.header.frame_id = this->input_msg_.header.frame_id;
  table_cloud_.header.stamp = ros::Time::now();

  seg->getObjectsOnTable(objects_on_table_);
  pcl::toROSMsg(*objects_on_table_, objects_on_table_cloud_);
  objects_on_table_cloud_.header.seq = 1;
  objects_on_table_cloud_.header.frame_id = this->input_msg_.header.frame_id;
  objects_on_table_cloud_.header.stamp = ros::Time::now();

  seg->getObjectsCloud(segmented_objects_cloud_);
  pcl::toROSMsg(*segmented_objects_cloud_, objects_cloud_);
  objects_cloud_.header.seq = 1;
  objects_cloud_.header.frame_id = this->input_msg_.header.frame_id;
  objects_cloud_.header.stamp = ros::Time::now();

  seg->getCutCloud(cut_cloud_);
  pcl::toROSMsg(*cut_cloud_, cut_cloud_ros_);
  cut_cloud_ros_.header.seq = 1;
  cut_cloud_ros_.header.frame_id = this->input_msg_.header.frame_id;
  cut_cloud_ros_.header.stamp = ros::Time::now();


  seg->getObjects(Objects_);
}

void SQFitter::getSuperquadricParameters(std::vector<sq_fitting::sq>& params)
{
  params.resize(0);
  for(int i=0;i<Objects_.size();++i)
  {
    SuperquadricFitting* sq_fit = new SuperquadricFitting(Objects_[i]);
    sq_fitting::sq min_param;
    double min_error;
    sq_fit->fit();
    sq_fit->getMinParams(min_param);
    sq_fit->getMinError(min_error);
    //ROS_INFO("Minimum error: %f ", min_error);
    ROS_INFO("======Parameters for Object[%d]======", i+1);
    ROS_INFO("a1: %f    a2:%f    a3:%f",min_param.a1, min_param.a2, min_param.a3 );
    ROS_INFO("e1: %f    e2:%f    ",min_param.e1, min_param.e2 );
    ROS_INFO("tx: %f    ty:%f    tz:%f",min_param.pose.position.x, min_param.pose.position.y ,min_param.pose.position.z );
    ROS_INFO("rx: %f    ry:%f     rz:%f   rw:%f", min_param.pose.orientation.x, min_param.pose.orientation.y,
             min_param.pose.orientation.z, min_param.pose.orientation.w);


    params.push_back(min_param);
    delete sq_fit;
  }
}

void SQFitter::sampleSuperquadrics(const std::vector<sq_fitting::sq>& params)
{
  CloudPtr sq_cloud_pcl_(new PointCloud);
  poseArr_.poses.resize(0);
  sqArr_.sqs.resize(0);
  for(int i=0;i<params.size();++i)
  {
    //Creating sqArray for publishing which will picked up by sq_grasp
    //sqArr_.sqs.push_back(params[i]);

    //Creating pose in output frame for visualizing
    geometry_msgs::Pose pose_out;
    transformFrame(params[i].pose, pose_out);
    poseArr_.poses.push_back(pose_out);
    //poseArr_.header.frame_id =  this->input_msg_.header.frame_id;
    poseArr_.header.frame_id =  output_frame_;
    poseArr_.header.stamp = ros::Time::now();


    sq_fitting::sq super;
    super.a1 = params[i].a1;
    super.a2 = params[i].a2;
    super.a3 = params[i].a3;
    super.e1 = params[i].e1;
    super.e2 = params[i].e2;
    super.pose = pose_out;
    sqArr_.sqs.push_back(super);

    Sampling* sam = new Sampling(params[i]);
    sam->sample_pilu_fisher();
    CloudPtr sq_cloud(new PointCloud);
    sam->getCloud(sq_cloud);
    for(size_t j=0;j<sq_cloud->points.size();++j)
    {
      PointT tmp;
      tmp.x = sq_cloud->points.at(j).x;
      tmp.y = sq_cloud->points.at(j).y;
      tmp.z = sq_cloud->points.at(j).z;
      tmp.r = sq_cloud->points.at(j).r;
      tmp.g = sq_cloud->points.at(j).g;
      tmp.b = sq_cloud->points.at(j).b;
      sq_cloud_pcl_->points.push_back(tmp);
    }
    delete sam;
  }
  sq_cloud_pcl_->height = 1;
  sq_cloud_pcl_->width = sq_cloud_pcl_->points.size();
  sq_cloud_pcl_->is_dense = true;
  pcl::toROSMsg(*sq_cloud_pcl_, sq_cloud_);
  sq_cloud_.header.seq = 1;
  sq_cloud_.header.frame_id = this->input_msg_.header.frame_id;
  sq_cloud_.header.stamp = ros::Time::now();


  sqArr_.header.frame_id = this->output_frame_;
  sqArr_.header.stamp = ros::Time::now();

}


void SQFitter::publishClouds()
{

  table_pub_.publish(table_cloud_);
  objects_pub_.publish(objects_cloud_);
  filtered_cloud_pub_.publish(filtered_cloud_ros_);
  objects_on_table_pub_.publish(objects_on_table_cloud_);
  superquadrics_pub_.publish(sq_cloud_);
  poses_pub_.publish(poseArr_);
  cut_cloud_pub_.publish(cut_cloud_ros_);
  sqs_pub_.publish(sqArr_);
}


void SQFitter::fit()
{
  if(this->initialized = true)
  {
    ros::Rate rate(1);
    ROS_INFO("Fitting superquadrics");
    while(ros::ok())
    {
      publishClouds();
      ros::spinOnce();
      rate.sleep();
    }
  }
}


