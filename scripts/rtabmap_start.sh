ros2 launch rtabmap_ros rtabmap.launch.py   rtabmap_args:=--delete_db_on_start   rgb_topic:=/camera/color/image_raw   depth_topic:=/camera/depth/image_rect_raw   camera_info_topic:=/camera/color/camera_info   approx_sync:=false   subscribe_stereo:=false   rgbd_odometry:=true   rtabmapviz:=true   rviz:=true
