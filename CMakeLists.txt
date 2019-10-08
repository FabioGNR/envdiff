cmake_minimum_required(VERSION 3.14)
project( vcvarstobash )

add_executable( vcvarstobash 
	src/main.cpp
)

set_property( TARGET vcvarstobash PROPERTY CXX_STANDARD 17)