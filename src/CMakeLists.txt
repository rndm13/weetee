add_library(json STATIC json.hpp json.cpp)
target_link_libraries(json PRIVATE nlohmann_json::nlohmann_json)

add_library(utils STATIC utils.hpp utils.cpp)
target_link_libraries(utils PRIVATE hello_imgui)

add_library(textinputcombo textinputcombo.hpp textinputcombo.cpp)
target_link_libraries(textinputcombo PRIVATE hello_imgui)
