CMAKE_MINIMUM_REQUIRED(VERSION 2.8.11)
PROJECT(QtPropertyBrowser)

##################### Look for required libraries ######################

# Add QT dependencies
FIND_PACKAGE(Qt5Widgets REQUIRED)

######################### Add Primary Targets ##########################
ADD_SUBDIRECTORY(src)
