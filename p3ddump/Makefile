CPP = $(wildcard *.cpp)
OBJ = $(CPP:%.cpp=build/%.o)
DEP = $(CPP:%.cpp=build/%.d)
CXXFLAGS=-I..

all: a.out

-include $(DEP)

a.out: $(OBJ) ../p3d.a
	c++ $^ $(CXXFLAGS)

build/%.o: %.cpp
	c++ -MMD $(CXXFLAGS) -c $< -o $@


