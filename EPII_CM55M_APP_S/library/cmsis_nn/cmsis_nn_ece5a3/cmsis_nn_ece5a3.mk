#"cmsis-nn ece5a3": Updated CMSIS-NN for YOLO26 support

# directory declaration
LIB_CMSIS_NN_DIR = $(LIBRARIES_ROOT)/cmsis_nn/cmsis_nn_ece5a3

LIB_CMSIS_NN_ASMSRCDIR		= $(LIB_CMSIS_NN_DIR) $(LIB_CMSIS_NN_ASMSRC_SUBDIR)
LIB_CMSIS_NN_CSRCDIR		= $(LIB_CMSIS_NN_DIR) $(LIB_CMSIS_NN_CSRC_SUBDIR)
LIB_CMSIS_NN_CXXSRCSDIR	= $(LIB_CMSIS_NN_DIR) $(LIB_CMSIS_NN_CXXSRC_SUBDIR)
LIB_CMSIS_NN_INCDIR		= $(LIB_CMSIS_NN_DIR) $(LIB_CMSIS_NN_INC_SUBDIR)

LIB_CMSIS_NN_CSRC_SUBDIR += $(LIB_CMSIS_NN_DIR)/Source/ActivationFunctions \
							 $(LIB_CMSIS_NN_DIR)/Source/BasicMathFunctions \
							 $(LIB_CMSIS_NN_DIR)/Source/ConcatenationFunctions \
							 $(LIB_CMSIS_NN_DIR)/Source/ConvolutionFunctions \
							 $(LIB_CMSIS_NN_DIR)/Source/FullyConnectedFunctions \
							 $(LIB_CMSIS_NN_DIR)/Source/LSTMFunctions \
						 	 $(LIB_CMSIS_NN_DIR)/Source/NNSupportFunctions \
							 $(LIB_CMSIS_NN_DIR)/Source/PadFunctions \
							 $(LIB_CMSIS_NN_DIR)/Source/PoolingFunctions \
					 	 	 $(LIB_CMSIS_NN_DIR)/Source/ReshapeFunctions \
							 $(LIB_CMSIS_NN_DIR)/Source/SoftmaxFunctions \
							 $(LIB_CMSIS_NN_DIR)/Source/SVDFunctions \
							 $(LIB_CMSIS_NN_DIR)/Source/TransposeFunctions \


LIB_CMSIS_NN_ASMSRC_SUBDIR +=
LIB_CMSIS_NN_CXXSRC_SUBDIR +=

LIB_CMSIS_NN_INC_SUBDIR += $(LIB_CMSIS_NN_DIR)/Include \
							$(LIB_CMSIS_NN_DIR)/Include/Internal \


# find all the source files in the target directories
LIB_CMSIS_NN_CSRCS   =	$(call get_csrcs, $(LIB_CMSIS_NN_CSRCDIR))
LIB_CMSIS_NN_CXXSRCS = $(call get_cxxsrcs, $(LIB_CMSIS_NN_CXXSRCSDIR))
LIB_CMSIS_NN_ASMSRCS = $(call get_asmsrcs, $(LIB_CMSIS_NN_ASMSRCDIR))

# get object files
LIB_CMSIS_NN_COBJS   = $(call get_relobjs, $(LIB_CMSIS_NN_CSRCS))
LIB_CMSIS_NN_CXXOBJS = $(call get_relobjs, $(LIB_CMSIS_NN_CXXSRCS))
LIB_CMSIS_NN_ASMOBJS = $(call get_relobjs, $(LIB_CMSIS_NN_ASMSRCS))
LIB_CMSIS_NN_OBJS    = $(LIB_CMSIS_NN_COBJS) $(LIB_CMSIS_NN_ASMOBJS) $(LIB_CMSIS_NN_CXXOBJS)

# get dependency files
LIB_CMSIS_NN_DEPS = $(call get_deps, $(LIB_CMSIS_NN_OBJS))

# extra macros to be defined
LIB_CMSIS_NN_DEFINES = -DLIB_CMSIS_NN -DARM_MATH_MVEI -DARM_MATH_DSP -DARM_MATH_LOOPUNROLL

# genearte library
ifeq ($(CMSIS_NN_LIB_FORCE_PREBUILT), y)
override LIB_CMSIS_NN_OBJS:=
endif
CMSIS_NN_LIB_NAME = lib_cmsis_nn_ece5a3.a
LIB_CMSIS_NN := $(subst /,$(PS), $(strip $(OUT_DIR)/$(CMSIS_NN_LIB_NAME)))

# library generation rule
$(LIB_CMSIS_NN): $(LIB_CMSIS_NN_OBJS)
	$(TRACE_ARCHIVE)
ifeq "$(strip $(LIB_CMSIS_NN_OBJS))" ""
	$(CP) $(PREBUILT_LIB)$(CMSIS_NN_LIB_NAME) $(LIB_CMSIS_NN)
else
	$(Q)$(AR) $(AR_OPT) $@ $(LIB_CMSIS_NN_OBJS)
	$(CP) $(LIB_CMSIS_NN) $(PREBUILT_LIB)$(CMSIS_NN_LIB_NAME)
endif


# specific compile rules
# user can add rules to compile this middleware
# if not rules specified to this middleware, it will use default compiling rules

# Middleware Definitions
LIB_INCDIR += $(LIB_CMSIS_NN_INCDIR)
LIB_CSRCDIR += $(LIB_CMSIS_NN_CSRCDIR)
LIB_CXXSRCDIR += $(LIB_CMSIS_NN_CXXSRCDIR)
LIB_ASMSRCDIR += $(LIB_CMSIS_NN_ASMSRCDIR)

LIB_CSRCS += $(LIB_CMSIS_NN_CSRCS)
LIB_CXXSRCS += $(LIB_CMSIS_NN_CXXSRCS)
LIB_ASMSRCS += $(LIB_CMSIS_NN_ASMSRCS)
LIB_ALLSRCS += $(LIB_CMSIS_NN_CSRCS) $(LIB_CMSIS_NN_ASMSRCS)

LIB_COBJS += $(LIB_CMSIS_NN_COBJS)
LIB_CXXOBJS += $(LIB_CMSIS_NN_CXXOBJS)
LIB_ASMOBJS += $(LIB_CMSIS_NN_ASMOBJS)
LIB_ALLOBJS += $(LIB_CMSIS_NN_OBJS)

LIB_DEFINES += $(LIB_CMSIS_NN_DEFINES)
LIB_DEPS += $(LIB_CMSIS_NN_DEPS)
LIB_LIBS += $(LIB_CMSIS_NN)
