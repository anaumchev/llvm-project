.PHONY: all reduction clean

OFFLOAD_ARCH ?= gfx90a
COMMON_FLAGS = -O2 -fopenmp --offload-arch=$(OFFLOAD_ARCH) -std=c++20 -save-temps=obj

PROJECTS = reduction
COMMON_HEADERS = common.h

RED_SRC = reduction.cpp
RED_HEADERS = reduction.h
# List of num_team values to build for.
RED_NUM_TEAMS = 208 10400

all: $(PROJECTS)

reduction: $(foreach TEAM,$(RED_NUM_TEAMS),reduction_$(TEAM))

reduction_%: $(RED_SRC) $(RED_HEADERS) $(COMMON_HEADERS)
	@echo "Building reduction for $* teams ..."
	rm -rf out_reduction_$*
	mkdir -p out_reduction_$*
	cd out_reduction_$* && $(CXX) -DXTEAM_NUM_TEAMS=$* $(COMMON_FLAGS) -o $@ $(addprefix ../,$(RED_SRC)) && cp $@ ..
	cd out_reduction_$* && $(dir $(CXX))llvm-dis *.bc

clean:
	rm -rf $(foreach TEAM,$(RED_NUM_TEAMS),reduction_$(TEAM))
	rm -rf $(foreach TEAM,$(RED_NUM_TEAMS),out_reduction_$(TEAM))
