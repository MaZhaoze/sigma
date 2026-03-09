CXX := g++

TARGET := engine.exe

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

OBJS := $(SRCS:.cpp=.o)

# -----------------------------
# 基础编译参数
# -----------------------------
WARN := -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion
STD  := -std=gnu++20

# 日常 release：已经很猛
BASE_OPT := -O3 -DNDEBUG -flto -fomit-frame-pointer -fno-plt -pipe -march=native -mtune=native

# 更激进一点，但一般仍然能跑
AGGR_OPT := -funroll-loops -falign-functions=32 -falign-loops=32 -fipa-pta -fdevirtualize -ftree-vectorize

# 如果你确定工程完全不用异常/RTTI，可以打开这两个
# 这俩有时还能再省一点体积/开销
NO_RTTI_EXC := -fno-exceptions -fno-rtti

# 你这套代码看起来没用到异常/RTTI，默认直接开
CXXFLAGS := $(STD) $(WARN) $(BASE_OPT) $(AGGR_OPT) $(NO_RTTI_EXC)
LDFLAGS  := -flto -s

# PGO
PGO_GEN_FLAGS := -fprofile-generate
PGO_USE_FLAGS := -fprofile-use -fprofile-correction

# -----------------------------
# 默认目标
# -----------------------------
all: release

release: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# -----------------------------
# 清理
# -----------------------------
clean:
	rm -f $(OBJS) $(TARGET)
	rm -rf profile-data

rebuild: clean release

# -----------------------------
# 仅 O3/LTO/native 的干净重编
# -----------------------------
fastclean:
	rm -f $(OBJS) $(TARGET)

# -----------------------------
# PGO 第一步：构建带采样的程序
# -----------------------------
pgo-build: clean
	mkdir -p profile-data
	$(MAKE) CXXFLAGS="$(CXXFLAGS) $(PGO_GEN_FLAGS)" LDFLAGS="$(LDFLAGS) $(PGO_GEN_FLAGS)" TARGET=$(TARGET)

# -----------------------------
# PGO 第二步：你自己跑一些典型搜索指令收集 profile
# 例子：
#   ./engine.exe
#   uci
#   isready
#   position startpos
#   go depth 8
#   go movetime 3000
#   quit
# 跑完后再 make pgo-use
# -----------------------------
pgo-use: fastclean
	$(MAKE) CXXFLAGS="$(CXXFLAGS) $(PGO_USE_FLAGS)" LDFLAGS="$(LDFLAGS) $(PGO_USE_FLAGS)" TARGET=$(TARGET)

# -----------------------------
# 调试版
# -----------------------------
debug: clean
	$(MAKE) CXXFLAGS="$(STD) -O0 -g3 -Wall -Wextra -Wshadow" LDFLAGS="" TARGET=$(TARGET)

.PHONY: all release clean rebuild fastclean pgo-build pgo-use debug