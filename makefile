# ══════════════════════════════════════════════════════════════
# Makefile — OS Process Scheduler Simulator
# ══════════════════════════════════════════════════════════════
#
# Usage:
#   make          → compile everything → produces ./scheduler
#   make run      → compile + run immediately
#   make clean    → remove binary and .o files
#   make debug    → compile with debugging symbols (for gdb)
#   make valgrind → run under valgrind (memory leak check)
#
# ══════════════════════════════════════════════════════════════

CC      = gcc
TARGET  = scheduler

# Source files
SRCS    = main.c scheduler.c thread_manager.c sync.c

# Object files (auto-generated from SRCS)
OBJS    = $(SRCS:.c=.o)

# Compiler flags
#   -Wall       : enable all warnings
#   -Wextra     : extra warnings
#   -pthread    : enable POSIX thread support
#   -g          : debug info (useful with gdb)
CFLAGS  = -Wall -Wextra -pthread -g

# Linker flags
#   -lpthread   : link pthread library
LDFLAGS = -lpthread

# ── DEFAULT TARGET ─────────────────────────────────────────────
all: $(TARGET)
	@echo ""
	@echo "  ✓ Build successful! Run with: ./$(TARGET)"
	@echo ""

# ── LINK STEP ──────────────────────────────────────────────────
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

# ── COMPILE EACH .c → .o ───────────────────────────────────────
%.o: %.c scheduler.h
	$(CC) $(CFLAGS) -c $< -o $@

# ── RUN ────────────────────────────────────────────────────────
run: all
	./$(TARGET)

# ── CLEAN ──────────────────────────────────────────────────────
clean:
	rm -f $(OBJS) $(TARGET)
	@echo "  Cleaned up build artifacts."

# ── DEBUG BUILD ────────────────────────────────────────────────
debug: CFLAGS += -DDEBUG -fsanitize=address,undefined
debug: all

# ── VALGRIND (memory leak check) ───────────────────────────────
valgrind: all
	valgrind --leak-check=full --track-origins=yes ./$(TARGET)

# ── PHONY TARGETS ──────────────────────────────────────────────
.PHONY: all run clean debug valgrind
