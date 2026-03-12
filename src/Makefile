CXX := g++

TARGET := sigma-chess

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin
PGO_DIR := $(BUILD_DIR)/pgo

SRCS := \
	main.cpp \
	Attack.cpp \
	Engine.cpp \
	Evaluation.cpp \
	MoveGeneration.cpp \
	OpeningBook.cpp \
	Position.cpp \
	Search.cpp \
	SearchCore.cpp \
	SearchOrder.cpp \
	SearchRoot.cpp \
	SearchThread.cpp \
	SearchTime.cpp \
	SearchTT.cpp \
	see_full.cpp \
	TT.cpp \
	types.cpp \
	uci.cpp

OBJS := $(SRCS:%.cpp=$(OBJ_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

ifeq ($(OS),Windows_NT)
	EXE := .exe
	RM_FILE = if exist "$(1)" del /Q /F "$(1)"
	RM_DIR  = if exist "$(1)" rmdir /S /Q "$(1)"
	MKDIR_P = if not exist "$(1)" mkdir "$(1)"
	RUN_BIN = .\build\bin\$(TARGET)$(EXE)
else
	EXE :=
	RM_FILE = rm -f "$(1)"
	RM_DIR  = rm -rf "$(1)"
	MKDIR_P = mkdir -p "$(1)"
	RUN_BIN = ./$(BIN_DIR)/$(TARGET)$(EXE)
endif

STD := -std=gnu++20
WARN := -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion
DEPFLAGS := -MMD -MP
DEFS := -DNDEBUG

COMMON_OPT := -O3 -fomit-frame-pointer -pipe
LTO_OPT := -flto=auto
NO_RTTI_EXC := -fno-exceptions -fno-rtti

# 默认开 AVX2
CPU_OPT := -mavx2 -mbmi2 -mpopcnt

PGO_GEN_FLAGS := -fprofile-generate="$(PGO_DIR)"
PGO_USE_FLAGS := -fprofile-use="$(PGO_DIR)" -fprofile-correction

RELEASE_CXXFLAGS := $(STD) $(WARN) $(DEPFLAGS) $(DEFS) \
	$(COMMON_OPT) $(LTO_OPT) $(NO_RTTI_EXC) $(CPU_OPT) $(PGO_USE_FLAGS)

RELEASE_LDFLAGS := $(LTO_OPT) -s $(PGO_USE_FLAGS)

PGO_GEN_CXXFLAGS := $(STD) $(WARN) $(DEPFLAGS) $(DEFS) \
	$(COMMON_OPT) $(LTO_OPT) $(NO_RTTI_EXC) $(CPU_OPT) $(PGO_GEN_FLAGS)

PGO_GEN_LDFLAGS := $(LTO_OPT) -s $(PGO_GEN_FLAGS)

NATIVE_CXXFLAGS := $(STD) $(WARN) $(DEPFLAGS) $(DEFS) \
	$(COMMON_OPT) $(LTO_OPT) $(NO_RTTI_EXC) $(CPU_OPT)

NATIVE_LDFLAGS := $(LTO_OPT) -s

DEBUG_CXXFLAGS := $(STD) $(WARN) $(DEPFLAGS) -O0 -g3
DEBUG_LDFLAGS :=

all: release

release: CXXFLAGS := $(RELEASE_CXXFLAGS)
release: LDFLAGS := $(RELEASE_LDFLAGS)
release: $(BIN_DIR)/$(TARGET)$(EXE)

native: CXXFLAGS := $(NATIVE_CXXFLAGS)
native: LDFLAGS := $(NATIVE_LDFLAGS)
native: $(BIN_DIR)/$(TARGET)$(EXE)

debug: CXXFLAGS := $(DEBUG_CXXFLAGS)
debug: LDFLAGS := $(DEBUG_LDFLAGS)
debug: $(BIN_DIR)/$(TARGET)$(EXE)

$(BIN_DIR)/$(TARGET)$(EXE): $(OBJS) | $(BIN_DIR)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: %.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	$(call MKDIR_P,$(BUILD_DIR))

$(OBJ_DIR): | $(BUILD_DIR)
	$(call MKDIR_P,$(OBJ_DIR))

$(BIN_DIR): | $(BUILD_DIR)
	$(call MKDIR_P,$(BIN_DIR))

$(PGO_DIR): | $(BUILD_DIR)
	$(call MKDIR_P,$(PGO_DIR))

pgo-build: clean | $(PGO_DIR)
	$(MAKE) release CXXFLAGS="$(PGO_GEN_CXXFLAGS)" LDFLAGS="$(PGO_GEN_LDFLAGS)"

pgo-use: clean-obj
	$(MAKE) release CXXFLAGS="$(RELEASE_CXXFLAGS)" LDFLAGS="$(RELEASE_LDFLAGS)"

pgo-train-file:
ifeq ($(OS),Windows_NT)
	-$(call RM_FILE,pgo-train.txt)
	@>> pgo-train.txt echo(uci
	@>> pgo-train.txt echo(isready
	@>> pgo-train.txt echo(ucinewgame
	@>> pgo-train.txt echo(position fen r1bqk1nr/pp1pppbp/2n3p1/8/3NP3/3BB3/PPP2PPP/RN1QK2R b KQkq - 3 6
	@>> pgo-train.txt echo(go depth 10
	@>> pgo-train.txt echo(position fen r1bqkb1r/1ppp1ppp/p1nn4/4p3/B7/5N2/PPPP1PPP/RNBQR1K1 w kq - 2 7
	@>> pgo-train.txt echo(go depth 10
	@>> pgo-train.txt echo(position fen r1bqkb1r/1ppp2pp/p1Nn1p2/8/B7/8/PPPP1PPP/RNBQR1K1 b kq - 0 8
	@>> pgo-train.txt echo(go depth 10
	@>> pgo-train.txt echo(position fen r1b1k2r/2ppb1pp/p2n1p2/8/p7/2N5/PPPPQPPP/R1B1R1K1 w kq - 0 12
	@>> pgo-train.txt echo(go depth 10
	@>> pgo-train.txt echo(quit
else
	@printf '%s\n' \
	'uci' \
	'isready' \
	'ucinewgame' \
	'position fen r1bqk1nr/pp1pppbp/2n3p1/8/3NP3/3BB3/PPP2PPP/RN1QK2R b KQkq - 3 6' \
	'go depth 10' \
	'position fen r1bqkb1r/1ppp1ppp/p1nn4/4p3/B7/5N2/PPPP1PPP/RNBQR1K1 w kq - 2 7' \
	'go depth 10' \
	'position fen r1bqkb1r/1ppp2pp/p1Nn1p2/8/B7/8/PPPP1PPP/RNBQR1K1 b kq - 0 8' \
	'go depth 10' \
	'position fen r1b1k2r/2ppb1pp/p2n1p2/8/p7/2N5/PPPPQPPP/R1B1R1K1 w kq - 0 12' \
	'go depth 10' \
	'quit' > pgo-train.txt
endif

pgo-train: pgo-train-file
	$(RUN_BIN) < pgo-train.txt

pgo-rebuild:
	$(MAKE) pgo-build
	$(MAKE) pgo-train
	$(MAKE) pgo-use

clean:
	-$(call RM_DIR,$(BUILD_DIR))
	-$(call RM_FILE,pgo-train.txt)

clean-obj:
	-$(call RM_DIR,$(OBJ_DIR))
	-$(call RM_DIR,$(BIN_DIR))

rebuild: clean all

.PHONY: all release native debug pgo-build pgo-use pgo-train-file pgo-train pgo-rebuild clean clean-obj rebuild

-include $(DEPS)