HIP_PATH ?= ..
DOXYGEN_SRC=$(wildcard $(HIP_PATH)/include/hcc_detail/*.h) $(wildcard $(HIP_PATH)/include/*.h) $(HIP_PATH)/src/hip_hcc.cpp $(wildcard doxygen-input/*)

doc:  $(DOXYGEN_SRC)
	HIP_PATH=$(HIP_PATH) doxygen doxygen-input/doxy.cfg
