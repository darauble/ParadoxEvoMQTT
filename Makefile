-include Config

#### Compilation flags ####
FLAGS_RELEASE = -O1
FLAGS_DEBUG = -Og -ggdb -DDEBUG

ifeq ($(BUILD), "RELEASE")
    C_FLAGS = $(FLAGS_RELEASE)
else ifeq ($(BUILD), "DEBUG")
    C_FLAGS = $(FLAGS_DEBUG)
else
    $(error "BUILD should be either RELEASE or DEBUG")
endif

SHARED_LIBS = -lzmq -lpthread -lpaho-mqtt3as
C_FLAGS += -std=c11 -Wall -c -fmessage-length=0 $(SHARED_LIBS)

SRC_DIR = src
BUILD_DIR = build
BINARY_NAME = paraevo

INCLUDES = \
    -I"$(SRC_DIR)/include"


OBJS = \
	$(BUILD_DIR)/$(SRC_DIR)/main.o \
	$(BUILD_DIR)/$(SRC_DIR)/mqtt_mgr.o \
	$(BUILD_DIR)/$(SRC_DIR)/para_mgr.o \
	$(BUILD_DIR)/$(SRC_DIR)/para_serial.o \
	$(BUILD_DIR)/$(SRC_DIR)/zmq_helpers.o

#### Targets ####
.PHONY: all clean

all: $(BINARY_NAME)

$(OBJS): $(BUILD_DIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(INCLUDES)  $(C_FLAGS) $(T_DEFINES) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"

$(BINARY_NAME): $(OBJS)
	@echo "Linking final binary $(BINARY_NAME)"
	$(CC) -o $(BINARY_NAME) $(OBJS) $(SHARED_LIBS)
ifeq ($(BUILD), "RELEASE")
	strip $(BINARY_NAME)
endif

clean:
	rm -rf $(BUILD_DIR) $(BINARY_NAME)
