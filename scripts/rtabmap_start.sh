ros2 launch rtabmap_ros rtabmap.launch.py   rtabmap_args:=--delete_db_on_start   rgb_topic:=/camera/color/image_raw   depth_topic:=/camera/depth/image_rect_raw   camera_info_topic:=/camera/color/camera_info   approx_sync:=true   subscribe_stereo:=false   visual_odometry:=true   publish_tf_map:=true  rtabmapviz:=true   rviz:=true
