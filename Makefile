gsqsolve: gsqsolve.cpp
	c++ --std=c++20 -Wall -Wextra -Wconversion -O3 $< -o $@
clean:
	rm -f gsqsolve
