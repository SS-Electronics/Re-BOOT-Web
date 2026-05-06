CC      = gcc
TARGET  = reboot-web
SRCDIR  = src
INCDIR  = inc
MGDIR   = mongoose

SRCS    = $(SRCDIR)/main.c   \
          $(SRCDIR)/db.c     \
          $(SRCDIR)/auth.c   \
          $(SRCDIR)/job.c    \
          $(SRCDIR)/routes.c \
          $(SRCDIR)/sha256.c \
          $(MGDIR)/mongoose.c

CFLAGS  = -Wall -Wextra -O2        \
          -I$(SRCDIR)              \
          -I$(INCDIR)              \
          -I$(MGDIR)               \
          -DMG_ENABLE_SSI=0

LDFLAGS = -lsqlite3 -lpthread

.PHONY: all clean fetch-mongoose

all: fetch-mongoose $(TARGET)

$(TARGET): $(SRCS)
	@echo "Linking $(TARGET)..."
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $(TARGET)
	@echo "Build complete: ./$(TARGET)"

fetch-mongoose:
	@if [ ! -f $(MGDIR)/mongoose.c ] || [ ! -f $(MGDIR)/mongoose.h ]; then \
		echo "Downloading Mongoose..."; \
		mkdir -p $(MGDIR); \
		curl -fsSL https://raw.githubusercontent.com/cesanta/mongoose/master/mongoose.c \
		     -o $(MGDIR)/mongoose.c; \
		curl -fsSL https://raw.githubusercontent.com/cesanta/mongoose/master/mongoose.h \
		     -o $(MGDIR)/mongoose.h; \
		echo "Mongoose downloaded."; \
	fi

clean:
	rm -f $(TARGET)

distclean: clean
	rm -rf $(MGDIR)
