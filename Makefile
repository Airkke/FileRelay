filerelay:main.cpp lib/base64.cpp
	g++ -o $@ $^ -std=c++17 -lpthread -lstdc++fs -ljsoncpp -lbundle -levent 
