set(common_lib ${CMAKE_CURRENT_LIST_DIR}/../../common/lib)

idf_component_register(
  SRCS main.cpp node.cpp setup.cpp Settings.cpp Detector.cpp
  INCLUDE_DIRS . ${common_lib}/CameraMsg ${common_lib}/MeshCoreFrames
  REQUIRES driver esp_timer esp_wifi esp_netif esp_event esp_http_server nvs_flash)
