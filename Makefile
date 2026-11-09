# Project Name
TARGET = MyDaisyVocoder

# Sources
CPP_SOURCES = MyDaisyVocoder.cpp \
              Controller.cpp \
              Engine.cpp \
              MidiMapper.cpp \
              MidiParser.cpp \
              Params.cpp \
              SettingsStorage.cpp \
              Synth.cpp \
              Vocoder.cpp

# Library Locations (override on the command line, e.g.
#   make LIBDAISY_DIR=/path/to/libDaisy DAISYSP_DIR=/path/to/DaisySP)
LIBDAISY_DIR ?= ../../libDaisy/
DAISYSP_DIR ?= ../../DaisySP/

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
