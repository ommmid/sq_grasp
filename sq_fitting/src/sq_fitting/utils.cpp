#include<sq_fitting/utils.h>
//#include <ceres/jet.h>


void sq_clampParameters(double& e1_clamped, double& e2_clamped)
{
  if(e1_clamped < 0.1)
    e1_clamped = 0.1;
  else if(e1_clamped > 1.9)
    e1_clamped = 1.9;
  if(e2_clamped < 0.1)
    e2_clamped = 0.1;
  else if(e2_clamped > 1.9)
    e2_clamped = 1.9;
}


double sq_function(const PointT& point, const sq_fitting::sq &param)
{
  double e1_clamped = param.e1;
  double e2_clamped = param.e2;
  sq_clampParameters(e1_clamped, e2_clamped);
  double t1 = pow(std::abs(point.x / param.a1), 2.0/e2_clamped);
  double t2 = pow(std::abs(point.y / param.a2), 2.0/e2_clamped);
  double t3 = pow(std::abs(point.z / param.a3), 2.0/e1_clamped);
  double f = pow(std::abs(t1+t2), e2_clamped/e1_clamped) + t3;
  return (pow(f,e1_clamped) - 1.0 );
}

double sq_function(const double &x, const double &y, const double &z, const double &a, const double &b, const double &c, const double &e1,
                   const double &e2)
{
  double e1_clamped = e1;
  double e2_clamped = e2;
  sq_clampParameters(e1_clamped, e2_clamped);

  //std::cout<<"e1: "<<e1_clamped<<" e2: "<<e2_clamped<<std::endl;
  double t1 = pow(std::abs(x / a), 2.0/e2_clamped);
  double t2 = pow(std::abs(y / b), 2.0/e2_clamped);
  double t3 = pow(std::abs(z / c), 2.0/e1_clamped);
  double f = pow(std::abs(t1+t2), e2_clamped/e1_clamped) + t3;
  double value = (pow(f, e1_clamped/2.0) - 1.0) * pow(a * b * c, 0.25);
  return (value);

}


double sq_function_scale_weighting(const PointT& point, const sq_fitting::sq &param)
{
  double e1_clamped = param.e1;
  double e2_clamped = param.e2;
  sq_clampParameters(e1_clamped, e2_clamped);
  double t1 = pow(std::abs(point.x / param.a1), 2.0/e2_clamped);
  double t2 = pow(std::abs(point.y /param.a2), 2.0/e2_clamped);
  double t3 = pow(std::abs(point.z /param.a3), 2.0/e1_clamped);
  double f = pow(std::abs(t1+t2), e2_clamped/e1_clamped) + t3;
  return (pow(f, e1_clamped/2.0) - 1.0) * pow(param.a1 * param.a2 * param.a3, 0.25);
}

double sq_error(const pcl::PointCloud<PointT>::Ptr cloud, const sq_fitting::sq &param)
{
  Eigen::Affine3f transform;
  sq_create_transform(param.pose, transform);
  pcl::PointCloud<PointT>::Ptr new_cloud(new pcl::PointCloud<PointT>);
  pcl::transformPointCloud(*cloud, *new_cloud, transform);
  double error = 0.0;
  for(size_t i = 0;i<new_cloud->size();++i)
  {
    double op = sq_normPoint(new_cloud->at(i));
    double val = op * sq_function_scale_weighting(new_cloud->at(i), param);
    error += val * val;
  }
  error /= new_cloud->size();
  return error;
}

void sq_create_transform(const geometry_msgs::Pose& pose, Eigen::Affine3f& transform)
{
  transform = Eigen::Affine3f::Identity();
  Eigen::Quaternionf q(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
  q.normalize();
  transform.translation()<<pose.position.x, pose.position.y, pose.position.z;
  transform.rotate(q);
}

double sq_normPoint(const PointT &point)
{
  double value = sqrt(point.x * point.x + point.y * point.y + point.z * point.z);
  return value;
}


void euler2Quaternion(const double roll, const double pitch, const double yaw, Eigen::Quaterniond &q)
{
  Eigen::AngleAxisd rollAngle(roll, Eigen::Vector3d::UnitX());
  Eigen::AngleAxisd pitchAngle(pitch, Eigen::Vector3d::UnitY());
  Eigen::AngleAxisd yawAngle(yaw, Eigen::Vector3d::UnitZ());
  q = yawAngle * pitchAngle * rollAngle;
}

void create_transformation_matrix(const double tx, const double ty, const double tz, const double ax, const double ay, const double az, Eigen::Affine3d &trns_mat)
{
  trns_mat = Eigen::Affine3d::Identity();
  Eigen::Affine3d rot_matrix = Eigen::Affine3d::Identity();
  create_rotation_matrix(ax, ay, az, rot_matrix);
  Eigen::Affine3d translation_matrix(Eigen::Translation3d(Eigen::Vector3d(tx, ty, tz)));
  trns_mat = rot_matrix * translation_matrix;
}

void create_rotation_matrix(const double ax, const double ay, const double az, Eigen::Affine3d &rot_matrix)
{
  Eigen::Affine3d rx = Eigen::Affine3d(Eigen::AngleAxisd(ax, Eigen::Vector3d(1,0,0)));
  Eigen::Affine3d ry = Eigen::Affine3d(Eigen::AngleAxisd(ay, Eigen::Vector3d(0,1,0)));
  Eigen::Affine3d rz = Eigen::Affine3d(Eigen::AngleAxisd(az, Eigen::Vector3d(0,0,1)));
  rot_matrix =  rz * ry * rx;
}

void getParamFromPose(const geometry_msgs::Pose &pose, double &tx, double &ty, double &tz, double &ax, double &ay, double &az)
{
  Eigen::Affine3f transformation = Eigen::Affine3f::Identity();
  sq_create_transform(pose, transformation);
  Eigen::Affine3d transformation_d = transformation.cast<double>();
  Eigen::Vector3d t = transformation_d.translation();
  Eigen::Matrix3d rot_matrix =transformation_d.rotation();
  tx = t(0);
  ty = t(1);
  tz = t(2);
  Eigen::Vector3d rot_angle = rot_matrix.eulerAngles(0,1,2);
  rot_angle[0] = ax;
  rot_angle[1] = ay;
  rot_angle[2] = az;
}

void Quaternion2Euler(const geometry_msgs::Pose &pose, double &ax, double &ay, double &az)
{
  Eigen::Quaterniond q(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
  q.normalize();
  Eigen::Vector3d euler = q.toRotationMatrix().eulerAngles(0,1,2);
  euler[0] = ax;
  euler[1] = ay;
  euler[2] = az;

}


void cutCloud(const pcl::PointCloud<PointT>::Ptr &input_cloud, pcl::PointCloud<PointT>::Ptr &output_cloud)
{
  //run through filters
  pcl::PointCloud<PointT>::Ptr cut_cloud_filtered(new pcl::PointCloud<PointT>);
  pcl::PointCloud<PointT>::Ptr cloud_stat_filtered(new pcl::PointCloud<PointT>);
  pcl::PointCloud<PointT>::Ptr cloud_voxel_filtered(new pcl::PointCloud<PointT>);
  pcl::PointCloud<PointT>::Ptr cloud_median_filtered(new pcl::PointCloud<PointT>);
  pcl::PointCloud<PointT>::Ptr cloud_bilateral_filtered(new pcl::PointCloud<PointT>);
  pcl::PointCloud<PointT>::Ptr mls_filtered(new pcl::PointCloud<PointT>);

  pcl::StatisticalOutlierRemoval<PointT> sor;
  sor.setInputCloud (input_cloud);
  sor.setMeanK (5);
  sor.setStddevMulThresh (0.8);
  sor.filter (* output_cloud);


  /*Eigen::Vector4f xyz_centroid;
    pcl::compute3DCentroid(*cloud_stat_filtered, xyz_centroid);
    for(size_t i=0;i<cloud_stat_filtered->points.size();++i)
      {
        if(cloud_stat_filtered->points[i].z < xyz_centroid(2)+0.02)
        {
              output_cloud->points.push_back(cloud_stat_filtered->points[i]);
            }

      }
      output_cloud->height = 1;
      output_cloud->header.frame_id = cloud_stat_filtered->header.frame_id;
      output_cloud->width = output_cloud->points.size();

       //Eigen::Affine3f transform = Eigen::Affine3f::Identity();*/
    //pcl::transformPointCloud (*cloud_stat_filtered, *output_cloud, transform);

  /*pcl::VoxelGrid<PointT> voxel;
  voxel.setInputCloud (cloud_stat_filtered);
  voxel.setLeafSize (0.01f, 0.01f, 0.01f);
  voxel.filter (*cloud_voxel_filtered);

  pcl::MedianFilter<PointT> median;
  median.setInputCloud(input_cloud);
  median.setMaxAllowedMovement(0.1);
  median.setWindowSize(1);
  median.applyFilter(*cloud_median_filtered);*/

  /*pcl::search::KdTree<PointT>::Ptr tree (new pcl::search::KdTree<PointT>);
  pcl::PointCloud<pcl::PointNormal> mls_points;
  pcl::MovingLeastSquares<PointT, pcl::PointNormal> mls;
  mls.setComputeNormals (true);
  mls.setInputCloud (cloud_stat_filtered);
  mls.setPolynomialFit (true);
  mls.setSearchMethod (tree);
  mls.setSearchRadius (0.03);
  mls.process (mls_points);
  pcl::copyPointCloud (mls_points, *output_cloud);*/


  /*Eigen::Vector4f xyz_centroid;
  pcl::compute3DCentroid(*input_cloud, xyz_centroid);
  for(size_t i=0;i<input_cloud->points.size();++i)
    {
      if(input_cloud->points[i].z < xyz_centroid(2))
      {
            cut_cloud_filtered->points.push_back(input_cloud->points[i]);
          }

    }
    cut_cloud_filtered->height = 1;
    cut_cloud_filtered->header.frame_id = input_cloud->header.frame_id;
    cut_cloud_filtered->width = cut_cloud_filtered->points.size();

     Eigen::Affine3f transform = Eigen::Affine3f::Identity();*/
  //pcl::transformPointCloud (*cloud_stat_filtered, *output_cloud, transform);


  /*pcl::MomentOfInertiaEstimation<PointT> feature_extractor;
  feature_extractor.setInputCloud (cut_cloud_filtered);
  feature_extractor.compute ();
  PointT  min_point_OBB;
  PointT  max_point_OBB;
  PointT position_OBB;
  Eigen::Matrix3f rotational_matrix_OBB;
  feature_extractor.getOBB (min_point_OBB, max_point_OBB, position_OBB, rotational_matrix_OBB);


  Eigen::Affine3f transform_1 = Eigen::Affine3f::Identity();
  transform_1.translation() << -position_OBB.x, -position_OBB.y, -position_OBB.z;
  pcl::PointCloud<PointT>::Ptr cloud_1(new pcl::PointCloud<PointT>());
  pcl::transformPointCloud (*cut_cloud_filtered, *cloud_1, transform_1);

  Eigen::Matrix4f transform_2 = Eigen::Matrix4f::Identity();
  transform_2.block<3,3>(0,0) = rotational_matrix_OBB.inverse();
  pcl::PointCloud<PointT>::Ptr cloud_2(new pcl::PointCloud<PointT>());
  pcl::transformPointCloud (*cloud_1, *cloud_2, transform_2);


  float theta = M_PI;
  Eigen::Affine3f transform_3 = Eigen::Affine3f::Identity();
  transform_3.rotate (Eigen::AngleAxisf (theta, Eigen::Vector3f::UnitY()));
  pcl::PointCloud<PointT>::Ptr cloud_3(new pcl::PointCloud<PointT>());
  pcl::transformPointCloud (*cloud_2, *cloud_3, transform_3);

  pcl::PointCloud<PointT>::Ptr cloud_4(new pcl::PointCloud<PointT>());
  for(size_t i=0;i<cloud_2->points.size();++i)
  {
    cloud_4->points.push_back(cloud_2->points.at(i));
    cloud_4->points.push_back(cloud_3->points.at(i));
  }

  Eigen::Matrix4f transform_4 = Eigen::Matrix4f::Identity();
  transform_4(0,3) = position_OBB.x;
  transform_4(1,3) = position_OBB.y ;
  transform_4(2,3) = position_OBB.z ;
  transform_4.block<3,3>(0,0) = rotational_matrix_OBB;

 pcl::transformPointCloud (*cloud_4, *output_cloud, transform_4);*/





}



