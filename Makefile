# Returns all c files nested or not in $(1)
define collect_sources
	$(shell find $(1) -name '*.c')
endef

SOURCES = $(call collect_sources, src)
OBJECTS = $(patsubst %.c, objects/%.o, $(SOURCES))

LD_FLAGS = -lncurses -lssl -lcrypto

.PHONY: build
all: build

build: $(OBJECTS)
	@echo "{Makefile} Creating the executable"
	@$(CC) $(OBJECTS) -o astrology $(LD_FLAGS)

	@./astrology gemini://geminispace.info/ 2>test.file

objects/%.o: %.c
	@# Making sure that the directory already exists before creating the object
	@mkdir -p $(dir $@)

	@echo "{Makefile} Building $@"
	@$(CC) -c $< -o $@

