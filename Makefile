CPP = $(wildcard *.cpp fake/*.cpp)
CPPX = $(notdir $(CPP))
OBJ = $(CPPX:%.cpp=build/%.o)
DEP = $(CPPX:%.cpp=build/%.d)
CXXFLAGS=-I. -g -Wall

all: p3d.a

-include $(DEP)

p3d.a: $(OBJ)
	ar rcs $@ $^

build/%.o: %.cpp
	c++ -MMD $(CXXFLAGS) -c $< -o $@
build/%.o: fake/%.cpp
	c++ -MMD $(CXXFLAGS) -c $< -o $@
