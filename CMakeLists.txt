cmake_minimum_required(VERSION 3.5)

project(MyBle LANGUAGES C)

add_executable(MyBle main.c utils.c utils.h configfile.c configfile.h customdatatypes.h)

#set_property(TARGET MyBle PROPERTY COMPILE_OPTIONS "-pthread")
target_link_libraries( MyBle pthread )
target_link_libraries( MyBle config )
target_link_libraries( MyBle bluetooth )
target_link_libraries( MyBle mosquitto )

install(TARGETS MyBle
   ARCHIVE DESTINATION lib
   LIBRARY DESTINATION lib
   RUNTIME DESTINATION bin)


