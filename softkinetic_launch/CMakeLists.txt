cmake_minimum_required(VERSION 2.8.3)
project(softkinetic_launch)

find_package(catkin)

catkin_package(CATKIN_DEPENDS softkinetic_camera softkinetic_description)

install(DIRECTORY launch
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)

install(DIRECTORY config
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)
