# No filename makefile with dependency control as per http://make.mad-scientist.net/papers/advanced-auto-dependency-generation/ and http://scottmcpeak.com/autodepend/autodepend.html                                                   
CFLAGS = -O2 -fPIC -g -Wno-write-strings -Isrc `pkg-config --cflags gstreamer-rtsp-server-1.0` -Igpmf-write
LIBS = -lpthread `pkg-config --libs gstreamer-rtsp-server-1.0` -lSDL2 -lgstaudio-1.0
SOURCE_DIR = src
BUILD_DIR = build-default
OUT_DIR = $(BUILD_DIR)/$(SOURCE_DIR)
DEP_DIR = $(BUILD_DIR)/dependency-rules

$(shell mkdir -p $(DEP_DIR) $(OUT_DIR) >/dev/null)
DEP_GEN_FLAGS = -MT $@ -MMD -MP -MF $(DEP_DIR)/$*.d

BIN = $(OUT_DIR)/run
SOURCES = $(wildcard $(SOURCE_DIR)/*.c*)
TMPOBJECTS := $(addprefix $(OUT_DIR)/,$(notdir $(SOURCES:.cpp=.o)))
OBJECTS := $(TMPOBJECTS:.c=.o)

#$(info $$SOURCES is [${SOURCES}])

$(BIN): $(OBJECTS)
	g++ $(OBJECTS) $(LIBS) -o $(BIN)

#$(DEP_DIR)/%.d: ;
#.PRECIOUS: $(DEP_DIR)/%.d
#

$(OUT_DIR)/%.o: $(SOURCE_DIR)/%.c
	gcc -c $(DEP_GEN_FLAGS) $(CFLAGS) $< -o $@

$(OUT_DIR)/%.o: $(SOURCE_DIR)/%.cpp
	g++ -c $(DEP_GEN_FLAGS) $(CFLAGS) $< -o $@

-include $(wildcard $(DEP_DIR)/*.d)

clean:
	rm -rf $(OUT_DIR) $(DEP_DIR)

