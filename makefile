# No filename makefile with dependency control as per http://make.mad-scientist.net/papers/advanced-auto-dependency-generation/
CC = g++
CFLAGS = -std=c++11 -O2 -fPIC -g -Wno-write-strings -Isrc
LIBS = -lpthread -lSDL
SOURCE_DIR = src
BUILD_DIR = build-default
OUT_DIR = $(BUILD_DIR)/$(SOURCE_DIR)
DEP_DIR = $(BUILD_DIR)/dependency-rules

DEP_GEN_FLAGS = -MT $@ -MMD -MP -MF $(DEP_DIR)/$*.Td
POSTCOMPILE = @mv -f $(DEP_DIR)/$*.Td $(DEP_DIR)/$*.d && touch $@

BIN = $(OUT_DIR)/run
SOURCES = $(wildcard $(SOURCE_DIR)/*.cpp)
OBJECTS := $(addprefix $(OUT_DIR)/,$(notdir $(SOURCES:.cpp=.o)))

$(shell mkdir -p $(DEP_DIR) $(OUT_DIR) >/dev/null)

$(BIN): $(OBJECTS)
  $(CC) $(OBJECTS) $(LIBS) -o $(BIN)


$(OUT_DIR)/%.o: $(SOURCE_DIR)/%.cpp $(DEP_DIR)/*.d
  $(CC) -c $(DEP_GEN_FLAGS) $(CFLAGS) $< -o $@
  $(POSTCOMPILE)

$(DEP_DIR)/%.d: ;
.PRECIOUS: $(DEP_DIR)/%.d

include $(wildcard $(DEP_DIR)/*.d)

clean:
  ./scripts/clean.sh
