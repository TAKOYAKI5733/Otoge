# ====================================================================
#  Otoge Project - Makefile for Ubuntu (Linux)
#  使い方: プロジェクトのフォルダ（src/ がある所）で  make
#          起動は                                      make run
#
#  事前に必要なパッケージ（初回だけ。make install-deps でも入れられる）:
#    sudo apt install build-essential pkg-config \
#                     libsdl2-dev libsdl2-ttf-dev libsdl2-mixer-dev libsdl2-image-dev \
#                     nlohmann-json3-dev
#
#  ⚠ Ubuntu 24.04 以降が必要です
#    このゲームは Mix_GetMusicPosition（SDL2_mixer 2.6 から）を使っています。
#    Ubuntu 22.04 の SDL2_mixer は 2.0.4 なので、そのままではビルドできません。
#    （make を実行すると、最初にバージョンを確かめて、足りなければ止めて知らせます）
#
#  コマンド:
#    make              → bin/otoge を作る
#    make run          → プロジェクトのフォルダで bin/otoge を起動する
#                        （scores/ fonts/ sounds/ jackets/ tutorial/ をそのまま使う。譜面エディタの保存もここに入る）
#    make DEBUG=1      → デバッグ用（最適化なし・gdb で追える）。obj_debug/ と bin/otoge_debug に分けて作る
#    make check-deps   → 必要なライブラリとバージョンがそろっているか確かめるだけ
#    make install-deps → 必要なパッケージを apt で入れる（sudo のパスワードを聞かれる）
#    make clean        → 作ったもの（obj*/ と bin/）を消す。譜面や設定は消さない
# ====================================================================

DEBUG ?= 0

CXX      := g++

# 使うライブラリ（pkg-config の名前）
PKGS     := sdl2 SDL2_ttf SDL2_mixer SDL2_image

# pkg-config --cflags で /usr/include/SDL2 を検索場所に入れる（ImGui の <SDL.h> 用）
#   ※ Windows 版と違い -Dmain=SDL_main は付けない（main.cpp は Linux ではふつうの main() になる）
PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS) 2>/dev/null)
PKG_LIBS   := $(shell pkg-config --libs $(PKGS) 2>/dev/null)

ifeq ($(DEBUG),1)
    OPTFLAGS := -O0 -g
    SUFFIX   := _debug
else
    OPTFLAGS := -O2
    SUFFIX   :=
endif

# -pthread: PreciseInput.h（キーを押した時刻を別スレッドで記録）などで std::thread を使うため
CXXFLAGS := -std=c++20 -Wall -Wextra $(OPTFLAGS) -MMD -MP -pthread $(PKG_CFLAGS)
LIBS     := $(PKG_LIBS) -pthread

SRC_DIR  := src
OBJ_DIR  := obj$(SUFFIX)
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/otoge$(SUFFIX)

# src/ 直下の .cpp と、src/imgui/ の .cpp（ImGui 本体と SDL2 用の部品）を全部ビルドする
#   ※ src/ から消したファイル（ScoreSync.cpp など）が残っていると一緒にビルドされるので、要らないものは消しておく
SRCS     := $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/imgui/*.cpp)
OBJS     := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
DEPS     := $(OBJS:.o=.d)

# 必要な最低バージョン（これより古いと、使っている関数が無くてビルドできない）
#   SDL2       2.0.18: SDL_RenderGeometry（角丸・傾けたレーン）、SDL_RenderCopyF など
#   SDL2_mixer 2.6.0 : Mix_GetMusicPosition（曲とのずれ直し）
#   SDL2_ttf   2.0.15: TTF_RenderUTF8_Blended_Wrapped など
#   SDL2_image 2.0.5 : IMG_LoadTexture など
MIN_SDL2   := 2.0.18
MIN_MIXER  := 2.6.0
MIN_TTF    := 2.0.15
MIN_IMAGE  := 2.0.5

# ---------------------------------------------------
# メインタスク
# ---------------------------------------------------
all: check-deps $(TARGET)
	@echo "==> 完成: $(TARGET)（make run で起動）"

# 1. リンク
$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS)

# 2. コンパイル（-I. はプロジェクト直下、-Isrc は "Play/xxx.h" などの検索場所）
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | check-deps
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -I$(SRC_DIR)/imgui -I. -c $< -o $@

# 3. 起動：プロジェクトのフォルダを作業フォルダにして起動する
#   ゲームは scores/ fonts/ sounds/ などを「今いるフォルダ」から読むので、ここで起動すれば素材をコピーしなくてよい
#   settings.json も、このフォルダに作られる
run: all
	./$(TARGET)

# 4. 必要なライブラリとバージョンの確認
#   pkg-config で「入っているか」「最低バージョン以上か」を見る。足りなければ、何を入れればよいかを出して止める
check-deps:
	@command -v pkg-config >/dev/null 2>&1 || { echo "NG: pkg-config がありません → sudo apt install pkg-config"; exit 1; }
	@ng=""; \
	check(){ \
	    if ! pkg-config --exists $$1; then ng="$$ng\n  $$1 が入っていません（apt: $$3）"; \
	    elif ! pkg-config --atleast-version=$$2 $$1; then ng="$$ng\n  $$1 が古いです（今: $$(pkg-config --modversion $$1)、必要: $$2 以上）"; fi; \
	}; \
	check sdl2       $(MIN_SDL2)  libsdl2-dev; \
	check SDL2_mixer $(MIN_MIXER) libsdl2-mixer-dev; \
	check SDL2_ttf   $(MIN_TTF)   libsdl2-ttf-dev; \
	check SDL2_image $(MIN_IMAGE) libsdl2-image-dev; \
	if [ ! -f /usr/include/nlohmann/json.hpp ] && [ ! -f /usr/local/include/nlohmann/json.hpp ]; then \
	    ng="$$ng\n  nlohmann/json.hpp がありません（apt: nlohmann-json3-dev）"; fi; \
	if [ -n "$$ng" ]; then \
	    printf "NG: ビルドに必要なものが足りません$$ng\n"; \
	    echo "  → make install-deps で入れられます"; \
	    echo "  ※ SDL2_mixer 2.6 以上は Ubuntu 24.04 以降の apt にあります（22.04 では古い版しか入りません）"; \
	    exit 1; \
	fi

# 5. 必要なパッケージを入れる
install-deps:
	sudo apt update
	sudo apt install -y build-essential pkg-config \
	    libsdl2-dev libsdl2-ttf-dev libsdl2-mixer-dev libsdl2-image-dev \
	    nlohmann-json3-dev

# 6. 削除（譜面・素材・settings.json は消さない）
clean:
	rm -rf obj obj_debug $(BIN_DIR)

-include $(DEPS)

.PHONY: all run check-deps install-deps clean