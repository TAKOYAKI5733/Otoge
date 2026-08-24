# ====================================================================
#  Otoge Project - Advanced Makefile (Header Dependency Resolution)
# ====================================================================

CXX      := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -MMD -MP

# 🌟 追加: sdl2-configの出力(SDL2のヘッダー実体があるパス、通常は /usr/include/SDL2)を
#          コンパイルオプションに追加。これにより <SDL.h>(サブフォルダなし直接指定)の
#          includeも解決できるようになる
SDL_CFLAGS := $(shell sdl2-config --cflags)

LIBS     := -lSDL2 -lSDL2_ttf -lSDL2_mixer

SRC_DIR  := src
OBJ_DIR  := obj
BIN_DIR  := bin

TARGET   := $(BIN_DIR)/otoge

SRCS     := $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/imgui/*.cpp)
OBJS     := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

DEPS     := $(OBJS:.o=.d)

ASSET_DIRS := sounds scores fonts

all: $(TARGET) copy_assets

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS)

copy_assets:
	@mkdir -p $(BIN_DIR)
	@for dir in $(ASSET_DIRS); do \
	        if [ -d $$dir ]; then \
	            echo "Copying $$dir to $(BIN_DIR)/..."; \
	            cp -r $$dir $(BIN_DIR)/; \
	        fi; \
	    done

# 🌟 修正: $(SDL_CFLAGS) を追加
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -I$(SRC_DIR)/imgui $(SDL_CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

-include $(DEPS)

.PHONY: all clean copy_assets