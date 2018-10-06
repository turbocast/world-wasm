CXX = em++
C99 = emcc -std=c99
LINK = em++
AR = emar
#DEBUG_FLAG=-g
CXXFLAGS = -O3 -Wall -fPIC $(DEBUG_FLAG) -D__linux__=1 --bind
CFLAGS = $(CXXFLAGS)
ARFLAGS = -rv
OUT_DIR = ./build
WORLD_DIR = ./World
OBJS = $(OUT_DIR)/objs/world_wrapper.o
LIBS = $(WORLD_DIR)/build/libworld.a

all: default
default: $(WORLD_DIR)/build/libworld.a $(OUT_DIR)/world_wrapper.o
	$(LINK) $(CXXFLAGS) -o $(OUT_DIR)/world_wrapper.js $(WORLD_DIR)/build/objs/tools/audioio.o $(OUT_DIR)/world_wrapper.o $(WORLD_DIR)/build/libworld.a -lm -s TOTAL_MEMORY=512MB #--preload-file test
$(WORLD_DIR)/build/libworld.a:
	@ (cd $(WORLD_DIR) ; $(MAKE) default)

###############################################################################################################
### Global rules
###############################################################################################################
$(OUT_DIR)/%.o : %.cpp
	mkdir -p $(OUT_DIR)
	$(CXX) $(CXXFLAGS) -I$(WORLD_DIR)/src -I$(WORLD_DIR)/tools -o "$@" -c "$<"

clean:
	rm -rf $(OUT_DIR)/*
	@ (cd $(WORLD_DIR) ; $(MAKE) clean)
