OUT ?= build/wii/probe
include $(DEVKITPPC)/wii_rules
.DEFAULT_GOAL := all
CFLAGS := -O2 -std=c11 -Wall -Wextra $(MACHDEP) -I$(LIBOGC_INC) -I$(DEVKITPRO)/portlibs/ppc/include -Iruntime -ffunction-sections -fdata-sections
LDFLAGS := $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/probe.map -L$(LIBOGC_LIB)
LIBS := -logc -lm
.PHONY: all
all: $(OUT)/probe.dol
$(OUT)/probe.o: wii/probe.c runtime/ppc_rt.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/probe.elf: $(OUT)/probe.o
	$(CC) $(LDFLAGS) $< $(LIBS) -o $@
$(OUT)/probe.dol: $(OUT)/probe.elf
	elf2dol $< $@
$(OUT)/thread_probe.o: wii/thread_probe.c wii/thread.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Iwii -frounding-math -ffp-contract=off -c $< -o $@
$(OUT)/thread_probe.elf: $(OUT)/thread_probe.o
	$(CC) $(LDFLAGS) $< $(LIBS) -o $@
$(OUT)/thread_probe.dol: $(OUT)/thread_probe.elf
	elf2dol $< $@

GAME ?= gticlub2
GAME_OUT ?= build/wii/$(GAME)
include generated/$(GAME)/sources.mk
GAME_CFLAGS := $(CFLAGS) $(EXTRA_CFLAGS) -DVIPER_WII -DDISP_BITS=12 -DVIPER_FIBER_STACK_SIZE=131072 \
 -DVIPER_SKIP_MEDIA_CHECK -frounding-math -ffp-contract=off -fno-strict-aliasing \
 -Wno-unused-variable -Wno-unused-label -Iwii -Igenerated/$(GAME)
GAME_GEN_OBJS := $(patsubst generated/$(GAME)/%.c,$(GAME_OUT)/generated/%.o,$(GEN_SRCS))
GEN_OPT ?= -O2
# Recompiled guest code only. The renderer keeps the rounding-mode flags.
GEN_FP :=
ifneq ($(findstring -DVIPER_WII_FAST_FP,$(EXTRA_CFLAGS)),)
GEN_FP := -fno-rounding-math -ffp-contract=fast
endif
GAME_CORE_OBJS := $(addprefix $(GAME_OUT)/runtime/,cpu.o sched.o hw.o)
DEVICE_LTO ?= 0
ifeq ($(DEVICE_LTO),1)
HOST_OPT ?= -O2
GAME_DEVICE_FLAGS := -flto $(filter-out -O2,$(HOST_OPT))
GAME_LINK_FLAGS := -flto $(HOST_OPT)
endif
GAME_PLATFORM_OBJS := $(GAME_OUT)/wii/pgo_dump.o $(GAME_OUT)/wii/net_report.o $(GAME_OUT)/wii/main.o $(GAME_OUT)/wii/voodoo_headless.o $(GAME_OUT)/wii/enhanced_headless.o $(GAME_OUT)/wii/media_check.o
ifneq ($(findstring VIPER_WII_ALLOC_LOG,$(EXTRA_CFLAGS)),)
GAME_PLATFORM_OBJS += $(GAME_OUT)/wii/alloc_log.o
GAME_ALLOC_WRAP := -Wl,--wrap=malloc,--wrap=memalign,--wrap=calloc
endif
ifneq ($(findstring -DVIPER_WII_AUDIO,$(EXTRA_CFLAGS)),)
GAME_PLATFORM_OBJS += $(GAME_OUT)/wii/audio.o
endif
ifeq ($(findstring -DVIPER_WII_SCRIPTED_RACE,$(EXTRA_CFLAGS)),)
GAME_PLATFORM_OBJS += $(GAME_OUT)/wii/input.o $(GAME_OUT)/wii/input_platform.o
GAME_INPUT_LIBS := -lwiiuse -lbte
endif
ifneq ($(findstring -DVIPER_WII_PC_PROFILE,$(EXTRA_CFLAGS)),)
GAME_PLATFORM_OBJS += $(GAME_OUT)/wii/pc_profile.o
endif
ifneq ($(findstring -DVIPER_WII_SUBMISSION_PROFILE,$(EXTRA_CFLAGS)),)
GAME_PLATFORM_OBJS += $(GAME_OUT)/wii/submission_profile.o
ifeq ($(findstring -DVIPER_WII_BULK_WRITER,$(EXTRA_CFLAGS)),)
$(GAME_OUT)/generated/gl_000.o: generated/$(GAME)/gl_000.c runtime/ppc_rt.h wii/submission_profile.h wii/instrument_submission.py
	@mkdir -p $(dir $@)
	python3 wii/instrument_submission.py $< $(GAME_OUT)/generated/gl_000.instrumented.c
	$(CC) $(GAME_CFLAGS) $(GEN_OPT) $(GEN_FP) -c $(GAME_OUT)/generated/gl_000.instrumented.c -o $@
endif
endif
ifneq ($(findstring -DVIPER_WII_SPLIT_RAM_HELPERS,$(EXTRA_CFLAGS)),)
GAME_PLATFORM_OBJS += $(GAME_OUT)/wii/ram_access_cold.o
endif
ifneq ($(findstring -DVIPER_WII_NATIVE_GL_DRAW,$(EXTRA_CFLAGS)),)
GAME_PLATFORM_OBJS += $(GAME_OUT)/wii/native_gl_draw.o
endif
ifneq ($(findstring -DVIPER_WII_NATIVE_GL_LIST,$(EXTRA_CFLAGS)),)
GAME_PLATFORM_OBJS += $(GAME_OUT)/wii/native_gl_list.o
endif
ifneq ($(findstring -DVIPER_WII_BULK_WRITER,$(EXTRA_CFLAGS)),)
$(GAME_OUT)/generated/gl_000.o: wii/specialize_gpr_multiple.py wii/gpr_multiple.h
$(GAME_OUT)/generated/gl_000.o: generated/$(GAME)/gl_000.c wii/specialize_direct_state.py wii/localize_function.py
ifneq ($(findstring -DVIPER_WII_SUBMISSION_PROFILE,$(EXTRA_CFLAGS)),)
$(GAME_OUT)/generated/gl_000.o: generated/$(GAME)/gl_000.c wii/submission_profile.h wii/instrument_submission.py
endif
$(GAME_OUT)/generated/gl_000.o: generated/$(GAME)/gl_000.c runtime/ppc_rt.h wii/voodoo_headless.h wii/specialize_submission.py wii/specialize_driving_writer.py wii/specialize_driving_tail.py wii/specialize_bounded_submission.py wii/bounded_submission.h wii/bulk_ram.h
	@mkdir -p $(dir $@)
	python3 wii/specialize_submission.py $(if $(findstring -DVIPER_WII_DEAD_CARRY,$(EXTRA_CFLAGS)),--dead-carry) $(if $(findstring -DVIPER_WII_BUDGET_LOCAL,$(EXTRA_CFLAGS)),--budget-local) $(if $(findstring -DVIPER_WII_DENSE_SWITCH,$(EXTRA_CFLAGS)),--dense-switch) $(if $(findstring -DVIPER_WII_DENSE_LR,$(EXTRA_CFLAGS)),--dense-lr) $(if $(findstring -DVIPER_WII_INLINE_LEAF ,$(EXTRA_CFLAGS) ),--inline-leaf) $(if $(findstring -DVIPER_WII_INLINE_LEAF_28FC4,$(EXTRA_CFLAGS)),--inline-leaf-28fc4) $(patsubst -DVIPER_WII_DIRECT_VERSION_MIN=%,--version-min %,$(filter -DVIPER_WII_DIRECT_VERSION_MIN=%,$(EXTRA_CFLAGS))) $(if $(findstring -DVIPER_WII_RESTRICT_CTX,$(EXTRA_CFLAGS)),--restrict-ctx) $< $(GAME_OUT)/generated/gl_000.bulk.c
ifneq ($(findstring -DVIPER_WII_SUBMISSION_PROFILE,$(EXTRA_CFLAGS)),)
	python3 wii/instrument_submission.py $(GAME_OUT)/generated/gl_000.bulk.c $(GAME_OUT)/generated/gl_000.instrumented.c
	$(CC) $(GAME_CFLAGS) $(GEN_OPT) $(GEN_FP) -c $(GAME_OUT)/generated/gl_000.instrumented.c -o $@
else
	$(CC) $(GAME_CFLAGS) $(GEN_OPT) $(GEN_FP) -c $(GAME_OUT)/generated/gl_000.bulk.c -o $@
endif
endif
ifneq ($(findstring -DVIPER_WII_GX_RENDER,$(EXTRA_CFLAGS)),)
GAME_PLATFORM_OBJS += $(GAME_OUT)/wii/gx_renderer.o $(GAME_OUT)/wii/texture.o $(GAME_OUT)/wii/menu.o $(GAME_OUT)/wii/projective_texture.o $(GAME_OUT)/wii/wdepth_split.o
endif
.PHONY: game
game: $(GAME_OUT)/viper.dol
$(GAME_OUT)/generated/%.o: generated/$(GAME)/%.c runtime/ppc_rt.h wii/specialize_gpr_multiple.py wii/gpr_multiple.h wii/native_ram.h wii/localize_function.py wii/localize_hot.txt wii/localize_hot_extra.txt wii/localize_hot2.txt
	@mkdir -p $(dir $@)
ifneq ($(findstring -DVIPER_WII_GPR_MULTIPLE,$(EXTRA_CFLAGS))$(findstring -DVIPER_WII_RAM_BASE_LOCAL,$(EXTRA_CFLAGS))$(findstring -DVIPER_WII_LOCALIZE_HOT,$(EXTRA_CFLAGS))$(findstring -DVIPER_WII_DEAD_CARRY,$(EXTRA_CFLAGS)),)
	python3 wii/specialize_gpr_multiple.py $(if $(findstring -DVIPER_WII_LOCALIZE_ALL,$(EXTRA_CFLAGS)),--localize) $(if $(findstring -DVIPER_WII_LOCALIZE_HOT,$(EXTRA_CFLAGS)),--localize-list wii/localize_hot.txt) $(if $(findstring -DVIPER_WII_LOCALIZE_EXTRA,$(EXTRA_CFLAGS)),--localize-list wii/localize_hot_extra.txt) $(if $(findstring -DVIPER_WII_LOCALIZE_HOT2,$(EXTRA_CFLAGS)),--localize-list wii/localize_hot2.txt) $(if $(findstring -DVIPER_WII_LANC_COPY,$(EXTRA_CFLAGS)),--lanc-copy) $(if $(findstring -DVIPER_WII_DEAD_CARRY,$(EXTRA_CFLAGS)),--dead-carry) $(if $(findstring -DVIPER_WII_BUDGET_LOCAL,$(EXTRA_CFLAGS)),--budget-local) $(if $(findstring -DVIPER_WII_DENSE_SWITCH,$(EXTRA_CFLAGS)),--dense-switch) $(if $(findstring -DVIPER_WII_RESTRICT_CTX,$(EXTRA_CFLAGS)),--restrict-ctx) $< $(GAME_OUT)/generated/$*.gpr.c
	$(CC) $(GAME_CFLAGS) $(GEN_OPT) $(GEN_FP) -c $(GAME_OUT)/generated/$*.gpr.c -o $@
else
	$(CC) $(GAME_CFLAGS) $(GEN_OPT) $(GEN_FP) -c $< -o $@
endif
$(GAME_OUT)/runtime/%.o: runtime/%.c $(wildcard runtime/*.h) wii/thread.h
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) $(GAME_DEVICE_FLAGS) -c $< -o $@
$(GAME_OUT)/runtime/cpu.o: wii/cr_unpack.h
$(GAME_OUT)/wii/%.o: wii/%.c $(wildcard wii/*.h) $(wildcard runtime/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) $(GAME_DEVICE_FLAGS) -c $< -o $@
$(GAME_OUT)/wii/gx_renderer.o $(GAME_OUT)/wii/voodoo_headless.o: wii/texture_layout.h
# Per-file optimization experiments: HOST_O3_FILES=native_gl_draw gx_renderer ...
HOST_O3_FILES ?=
$(foreach f,$(HOST_O3_FILES),$(eval $(GAME_OUT)/wii/$(f).o: GAME_DEVICE_FLAGS += -O3))
$(GAME_OUT)/wii/media_check.o: wii/media_check.c runtime/ppc_rt.h
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) -c $< -o $@
$(GAME_OUT)/viper.elf: $(GAME_GEN_OBJS) $(GAME_CORE_OBJS) $(GAME_PLATFORM_OBJS)
	$(CC) $(MACHDEP) $(GAME_LINK_FLAGS) $(GAME_ALLOC_WRAP) -Wl,--gc-sections,-Map,$(GAME_OUT)/viper.map -L$(LIBOGC_LIB) -L$(DEVKITPRO)/portlibs/ppc/lib $^ -lfat $(GAME_INPUT_LIBS) -lz -logc -lm -o $@
$(GAME_OUT)/viper.dol: $(GAME_OUT)/viper.elf
	elf2dol $< $@

.PHONY: gx-probe
.PHONY: bulk-writer-probe
bulk-writer-probe: $(OUT)/bulk_writer_probe.dol
$(OUT)/bulk_writer_probe.c: wii/test_bulk_writer.py wii/specialize_submission.py wii/bulk_ram.h generated/gticlub2/gl_000.c wii/voodoo_headless_test.c wii/voodoo_headless.c wii/voodoo_headless.h
	@mkdir -p $(OUT)
	python3 wii/test_bulk_writer.py $(BULK_WRITER_PROBE_ARGS) --emit-native $@
$(OUT)/bulk_writer_probe.o: $(OUT)/bulk_writer_probe.c runtime/ppc_rt.h
	$(CC) $(CFLAGS) $(BULK_WRITER_PROBE_FLAGS) $(BULK_WRITER_PROBE_MEMO_FLAGS) -Iwii -Itests/fixtures -DVIPER_WII_BULK_WRITER -frounding-math -ffp-contract=off -c $< -o $@
$(OUT)/bulk_writer_probe.elf: $(OUT)/bulk_writer_probe.o $(BULK_WRITER_PROBE_EXTRA_OBJS)
	$(CC) $(MACHDEP) $(BULK_WRITER_PROBE_LINK_FLAGS) -Wl,--gc-sections,-Map,$(OUT)/bulk_writer_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/bulk_writer_probe.dol: $(OUT)/bulk_writer_probe.elf
	elf2dol $< $@
.PHONY: fog-probe
.PHONY: tmu-equation-probe
.PHONY: tmu-pipeline-probe
.PHONY: split-depth-probe
.PHONY: constant-fog-probe
constant-fog-probe: $(OUT)/constant_fog_probe.dol
$(OUT)/constant_fog_probe.o: wii/constant_fog_probe.c wii/gx_constant_fog.h wii/gx_fog_stage.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -Iwii -c $< -o $@
$(OUT)/constant_fog_probe.elf: $(OUT)/constant_fog_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/constant_fog_probe.map -L$(LIBOGC_LIB) $^ -lfat $(LIBS) -o $@
$(OUT)/constant_fog_probe.dol: $(OUT)/constant_fog_probe.elf
	elf2dol $< $@
split-depth-probe: $(OUT)/split_depth_probe.dol
$(OUT)/split_depth_probe.o: wii/split_depth_probe.c wii/gx_color_equation.h wii/gx_rejection.h wii/gx_fog_stage.h wii/voodoo_color_plan.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -Iwii -c $< -o $@
$(OUT)/split_depth_probe.elf: $(OUT)/split_depth_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/split_depth_probe.map -L$(LIBOGC_LIB) $^ -lfat $(LIBS) -o $@
$(OUT)/split_depth_probe.dol: $(OUT)/split_depth_probe.elf
	elf2dol $< $@
tmu-pipeline-probe: $(OUT)/tmu_pipeline_probe.dol
$(OUT)/tmu_pipeline_probe.o: wii/tmu_pipeline_probe.c wii/gx_tmu_equation.h wii/gx_color_equation.h wii/gx_rejection.h wii/gx_fog_stage.h wii/voodoo_tmu_plan.h wii/voodoo_tmu_eval.h wii/voodoo_color_eval.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -Iwii -c $< -o $@
$(OUT)/tmu_pipeline_probe.elf: $(OUT)/tmu_pipeline_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/tmu_pipeline_probe.map -L$(LIBOGC_LIB) $^ -lfat $(LIBS) -o $@
$(OUT)/tmu_pipeline_probe.dol: $(OUT)/tmu_pipeline_probe.elf
	elf2dol $< $@
tmu-equation-probe: $(OUT)/tmu_equation_probe.dol
$(OUT)/tmu_equation_probe.o: wii/tmu_equation_probe.c wii/gx_tmu_equation.h wii/voodoo_tmu_plan.h wii/voodoo_tmu_eval.h wii/voodoo_color_eval.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -Iwii -c $< -o $@
$(OUT)/tmu_equation_probe.elf: $(OUT)/tmu_equation_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/tmu_equation_probe.map -L$(LIBOGC_LIB) $^ -lfat $(LIBS) -o $@
$(OUT)/tmu_equation_probe.dol: $(OUT)/tmu_equation_probe.elf
	elf2dol $< $@
fog-probe: $(OUT)/fog_probe.dol
$(OUT)/fog_probe.o: wii/fog_probe.c wii/fog.h wii/gx_fog_stage.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -Iwii -c $< -o $@
$(OUT)/fog_probe.elf: $(OUT)/fog_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/fog_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/fog_probe.dol: $(OUT)/fog_probe.elf
	elf2dol $< $@
.PHONY: chroma-probe
.PHONY: color-probe
.PHONY: alpha-probe
.PHONY: alpha-mask-probe
alpha-mask-probe: $(OUT)/alpha_mask_probe.dol
$(OUT)/alpha_mask_probe.o: wii/alpha_mask_probe.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(OUT)/alpha_mask_probe.elf: $(OUT)/alpha_mask_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/alpha_mask_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/alpha_mask_probe.dol: $(OUT)/alpha_mask_probe.elf
	elf2dol $< $@
alpha-probe: $(OUT)/alpha_probe.dol
$(OUT)/alpha_probe.o: wii/alpha_probe.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(OUT)/alpha_probe.elf: $(OUT)/alpha_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/alpha_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/alpha_probe.dol: $(OUT)/alpha_probe.elf
	elf2dol $< $@
color-probe: $(OUT)/color_probe.dol
$(OUT)/color_probe.o: wii/color_probe.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(OUT)/color_probe.elf: $(OUT)/color_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/color_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/color_probe.dol: $(OUT)/color_probe.elf
	elf2dol $< $@
chroma-probe: $(OUT)/chroma_probe.dol
iterated-mask-probe: $(OUT)/iterated_mask_probe.dol
$(OUT)/iterated_mask_probe.o: wii/iterated_mask_probe.c wii/gx_rejection.h wii/render_family.h wii/voodoo_color_plan.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(OUT)/iterated_mask_probe.elf: $(OUT)/iterated_mask_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/iterated_mask_probe.map -L$(LIBOGC_LIB) $^ -lfat $(LIBS) -o $@
$(OUT)/iterated_mask_probe.dol: $(OUT)/iterated_mask_probe.elf
	elf2dol $< $@
color-equation-probe: $(OUT)/color_equation_probe.dol
$(OUT)/color_equation_probe.o: wii/color_equation_probe.c wii/gx_color_equation.h wii/gx_rejection.h wii/gx_fog_stage.h wii/voodoo_color_plan.h wii/voodoo_color_eval.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(OUT)/color_equation_probe.elf: $(OUT)/color_equation_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/color_equation_probe.map -L$(LIBOGC_LIB) $^ -lfat $(LIBS) -o $@
$(OUT)/color_equation_probe.dol: $(OUT)/color_equation_probe.elf
	elf2dol $< $@
$(OUT)/chroma_probe.o: wii/chroma_probe.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(OUT)/chroma_probe.elf: $(OUT)/chroma_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/chroma_probe.map -L$(LIBOGC_LIB) $^ -lfat $(LIBS) -o $@
$(OUT)/chroma_probe.dol: $(OUT)/chroma_probe.elf
	elf2dol $< $@
gx-probe: $(OUT)/gx_probe.dol
$(OUT)/gx_probe.o: wii/gx_probe.c wii/texture.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(OUT)/gx_texture.o: wii/texture.c wii/texture.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(OUT)/gx_probe.elf: $(OUT)/gx_probe.o $(OUT)/gx_texture.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/gx_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/gx_probe.dol: $(OUT)/gx_probe.elf
	elf2dol $< $@

.PHONY: tilt-probe
tilt-probe: $(OUT)/tilt_probe.dol
$(OUT)/tilt_probe.o: wii/tilt_probe.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/tilt_probe.elf: $(OUT)/tilt_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections -L$(LIBOGC_LIB) $^ -lwiiuse -lbte -lfat $(LIBS) -o $@
$(OUT)/tilt_probe.dol: $(OUT)/tilt_probe.elf
	elf2dol $< $@

# Remote SD card helper (wii/sd_tool.c, driven by wii/sd_remote.py).
.PHONY: sd-tool
sd-tool: build/wii/sd_tool/sd_tool.dol
build/wii/sd_tool/sd_tool.o: wii/sd_tool.c
	@mkdir -p build/wii/sd_tool
	$(CC) $(CFLAGS) -c $< -o $@
build/wii/sd_tool/sd_tool.elf: build/wii/sd_tool/sd_tool.o
	$(CC) $(MACHDEP) -Wl,--gc-sections -L$(LIBOGC_LIB) -L$(DEVKITPRO)/portlibs/ppc/lib $^ -lfat -lz $(LIBS) -o $@
build/wii/sd_tool/sd_tool.dol: build/wii/sd_tool/sd_tool.elf
	elf2dol $< $@

.PHONY: input-schedule-probe
input-schedule-probe: $(OUT)/input_schedule_probe.dol
$(OUT)/input_schedule_probe.o: wii/input_schedule_probe.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(OUT)/input_schedule_probe.elf: $(OUT)/input_schedule_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/input_schedule_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/input_schedule_probe.dol: $(OUT)/input_schedule_probe.elf
	elf2dol $< $@

.PHONY: projective-probe
projective-probe: $(OUT)/projective_probe.dol
$(OUT)/projective_probe.o: wii/projective_probe.c wii/projective_texture.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(OUT)/projective_texture.o: wii/projective_texture.c wii/projective_texture.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(OUT)/projective_probe.elf: $(OUT)/projective_probe.o $(OUT)/projective_texture.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/projective_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/projective_probe.dol: $(OUT)/projective_probe.elf
	elf2dol $< $@

.PHONY: wdepth-probe
wdepth-probe: $(OUT)/wdepth_probe.dol
$(OUT)/wdepth_probe.o: wii/wdepth_probe.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(OUT)/wdepth_probe.elf: $(OUT)/wdepth_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/wdepth_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/wdepth_probe.dol: $(OUT)/wdepth_probe.elf
	elf2dol $< $@

.PHONY: dual-coord-probe
dual-coord-probe: $(OUT)/dual_coord_probe.dol
$(OUT)/dual_coord_probe.o: wii/dual_coord_probe.c wii/projective_texture.h wii/gx_tmu_equation.h wii/voodoo_tmu_eval.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -Iwii -c $< -o $@
$(OUT)/dual_coord_probe.elf: $(OUT)/dual_coord_probe.o $(OUT)/projective_texture.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/dual_coord_probe.map -L$(LIBOGC_LIB) $^ -lfat $(LIBS) -o $@
$(OUT)/dual_coord_probe.dol: $(OUT)/dual_coord_probe.elf
	elf2dol $< $@


.PHONY: borrow-clip-probe
borrow-clip-probe: $(OUT)/borrow_clip_probe.dol
$(OUT)/borrow_clip_probe.c: wii/test_borrow_clip.py
	@mkdir -p $(OUT)
	python3 wii/test_borrow_clip.py --emit-native $@
$(OUT)/borrow_clip_probe.o: $(OUT)/borrow_clip_probe.c wii/wdepth_split.h
	$(CC) $(CFLAGS) -Iwii -frounding-math -ffp-contract=off -Werror -c $< -o $@
$(OUT)/borrow_clip_reference.o: wii/wdepth_split.c wii/wdepth_split.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Iwii -DVIPER_WII_WDEPTH_INTERIOR -Dwii_wdepth_split=reference_split -Dwii_wdepth_band=reference_band -frounding-math -ffp-contract=off -Werror -c $< -o $@
$(OUT)/borrow_clip_candidate.o: wii/wdepth_split.c wii/wdepth_split.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Iwii -DVIPER_WII_WDEPTH_INTERIOR -DVIPER_WII_WDEPTH_BORROW_CLIP -frounding-math -ffp-contract=off -Werror -c $< -o $@
$(OUT)/borrow_clip_probe.elf: $(OUT)/borrow_clip_probe.o $(OUT)/borrow_clip_reference.o $(OUT)/borrow_clip_candidate.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/borrow_clip_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/borrow_clip_probe.dol: $(OUT)/borrow_clip_probe.elf
	elf2dol $< $@

.PHONY: bounded-submission-probe
.PHONY: split-copy-probe
split-copy-probe: $(OUT)/split_copy_probe.dol
$(OUT)/split_copy_probe.c: wii/test_borrow_clip.py
	@mkdir -p $(OUT)
	python3 wii/test_borrow_clip.py --copy-once --emit-native $@
$(OUT)/split_copy_probe.o: $(OUT)/split_copy_probe.c wii/wdepth_split.h
	$(CC) $(CFLAGS) -Iwii -frounding-math -ffp-contract=off -Werror -c $< -o $@
$(OUT)/split_copy_reference.o: wii/wdepth_split.c wii/wdepth_split.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Iwii -DVIPER_WII_WDEPTH_INTERIOR -DVIPER_WII_WDEPTH_BORROW_CLIP -Dwii_wdepth_split=reference_split -Dwii_wdepth_band=reference_band -frounding-math -ffp-contract=off -Werror -c $< -o $@
$(OUT)/split_copy_candidate.o: wii/wdepth_split.c wii/wdepth_split.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Iwii -DVIPER_WII_WDEPTH_INTERIOR -DVIPER_WII_WDEPTH_BORROW_CLIP -DVIPER_WII_WDEPTH_COPY_ONCE -frounding-math -ffp-contract=off -Werror -c $< -o $@
$(OUT)/split_copy_probe.elf: $(OUT)/split_copy_probe.o $(OUT)/split_copy_reference.o $(OUT)/split_copy_candidate.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/split_copy_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/split_copy_probe.dol: $(OUT)/split_copy_probe.elf
	elf2dol $< $@

bounded-submission-probe: $(OUT)/bounded_submission_probe.dol
$(OUT)/bounded_submission_probe.c: wii/test_bounded_submission.py wii/specialize_bounded_submission.py generated/gticlub2/gl_000.c
	@mkdir -p $(OUT)
	python3 wii/test_bounded_submission.py --emit-native $@
$(OUT)/bounded_submission_probe.o: $(OUT)/bounded_submission_probe.c wii/bounded_submission.h runtime/ppc_rt.h
	$(CC) $(CFLAGS) -Iwii -Itests/fixtures -DVIPER_WII_BOUNDED_SUBMISSION -frounding-math -ffp-contract=off -Werror -c $< -o $@
$(OUT)/bounded_submission_probe.elf: $(OUT)/bounded_submission_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/bounded_submission_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/bounded_submission_probe.dol: $(OUT)/bounded_submission_probe.elf
	elf2dol $< $@

.PHONY: merged-lookup-probe
merged-lookup-probe: $(OUT)/merged_lookup_probe.dol
$(OUT)/merged_lookup_probe.o: wii/merged_lookup_probe.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/merged_lookup_probe.elf: $(OUT)/merged_lookup_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/merged_lookup_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/merged_lookup_probe.dol: $(OUT)/merged_lookup_probe.elf
	elf2dol $< $@



.PHONY: lookup-bind-probe
lookup-bind-probe: $(OUT)/lookup_bind_probe.dol
$(OUT)/lookup_bind_probe.o: wii/lookup_bind_probe.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(OUT)/lookup_bind_probe.elf: $(OUT)/lookup_bind_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/lookup_bind_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/lookup_bind_probe.dol: $(OUT)/lookup_bind_probe.elf
	elf2dol $< $@


.PHONY: texture-resource-probe
texture-resource-probe: $(OUT)/texture_resource_probe.dol
$(OUT)/texture_resource_probe.o: wii/texture_resource_probe.c wii/gx_texture_resource.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Iwii -frounding-math -ffp-contract=off -Werror -c $< -o $@
$(OUT)/texture_resource_probe.elf: $(OUT)/texture_resource_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/texture_resource_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/texture_resource_probe.dol: $(OUT)/texture_resource_probe.elf
	elf2dol $< $@


.PHONY: split-ram-float-probe
split-ram-float-probe: $(OUT)/split_ram_float_probe.dol
$(OUT)/split_ram_float_probe.c: wii/test_split_ram_float.py runtime/ppc_rt.h
	@mkdir -p $(OUT)
	python3 wii/test_split_ram_float.py --emit-native $@
$(OUT)/split_ram_float_probe.o: $(OUT)/split_ram_float_probe.c runtime/ppc_rt.h
	$(CC) $(CFLAGS) -DVIPER_WII_SPLIT_RAM_HELPERS -DVIPER_MEMORY_AUDIT -frounding-math -ffp-contract=off -c $< -o $@
$(OUT)/split_ram_float_cold.o: wii/ram_access_cold.c runtime/ppc_rt.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -DVIPER_WII_SPLIT_RAM_HELPERS -DVIPER_MEMORY_AUDIT -c $< -o $@
$(OUT)/split_ram_float_probe.elf: $(OUT)/split_ram_float_probe.o $(OUT)/split_ram_float_cold.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/split_ram_float_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/split_ram_float_probe.dol: $(OUT)/split_ram_float_probe.elf
	elf2dol $< $@

.PHONY: f64-ram-probe
f64-ram-probe: $(OUT)/f64_ram_probe.dol
$(OUT)/f64_ram_probe.c: wii/test_split_ram_float.py runtime/ppc_rt.h
	@mkdir -p $(OUT)
	python3 wii/test_split_ram_float.py --f64-ram --emit-native $@
$(OUT)/f64_ram_probe.o: $(OUT)/f64_ram_probe.c runtime/ppc_rt.h
	$(CC) $(CFLAGS) -DVIPER_WII_SPLIT_RAM_HELPERS -frounding-math -ffp-contract=off -c $< -o $@
$(OUT)/f64_ram_probe.elf: $(OUT)/f64_ram_probe.o $(OUT)/split_ram_float_cold.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/f64_ram_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/f64_ram_probe.dol: $(OUT)/f64_ram_probe.elf
	elf2dol $< $@

# Native split-RAM helper probe.
# Include from the repository root after wii/Makefile has included
# $(DEVKITPPC)/wii_rules and set OUT, CFLAGS, and LIBS (-logc -lm).
# This does not change the default goal or any production object.
# `make split-ram-probe` emits the harness, compiles it plus the real
# wii/ram_access_cold.c, links libogc, and converts the ELF with elf2dol.

.PHONY: split-ram-probe
split-ram-probe: $(OUT)/split_ram_probe.dol

$(OUT)/split_ram_probe.c: wii/test_split_ram_helpers.py runtime/ppc_rt.h
	@mkdir -p $(OUT)
	python3 wii/test_split_ram_helpers.py --emit-native $@

$(OUT)/split_ram_probe.o: $(OUT)/split_ram_probe.c runtime/ppc_rt.h
	$(CC) $(CFLAGS) -Iruntime -DVIPER_WII_SPLIT_RAM_HELPERS -DVIPER_MEMORY_AUDIT -c $< -o $@

$(OUT)/split_ram_access_cold.o: wii/ram_access_cold.c runtime/ppc_rt.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -Iruntime -DVIPER_WII_SPLIT_RAM_HELPERS -DVIPER_MEMORY_AUDIT -c $< -o $@

$(OUT)/split_ram_probe.elf: $(OUT)/split_ram_probe.o $(OUT)/split_ram_access_cold.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/split_ram_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@

$(OUT)/split_ram_probe.dol: $(OUT)/split_ram_probe.elf
	elf2dol $< $@

.PHONY: driving-writer-probe

driving-writer-probe: $(OUT)/driving_writer_probe.dol
$(OUT)/driving_writer_probe.c: wii/test_driving_writer.py wii/specialize_driving_writer.py wii/specialize_driving_tail.py wii/driving_writer_probe_body.c wii/test_bulk_writer.py wii/specialize_submission.py wii/voodoo_headless_test.c wii/voodoo_headless.c generated/gticlub2/gl_000.c
	@mkdir -p $(OUT)
	python3 wii/test_driving_writer.py $(DRIVING_WRITER_PROBE_ARGS) --emit-native $@
$(OUT)/driving_writer_probe.o: $(OUT)/driving_writer_probe.c runtime/ppc_rt.h wii/voodoo_headless.h
	$(CC) $(CFLAGS) -Iwii -Itests/fixtures -DVIPER_WII_BULK_WRITER -fno-strict-aliasing -frounding-math -ffp-contract=off -c $< -o $@
$(OUT)/driving_writer_probe.elf: $(OUT)/driving_writer_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/driving_writer_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/driving_writer_probe.dol: $(OUT)/driving_writer_probe.elf
	elf2dol $< $@

.PHONY: 

.PHONY: arithmetic-island-probe
arithmetic-island-probe: $(OUT)/arithmetic_island_probe.dol
$(OUT)/arithmetic_island_probe.c: wii/test_arithmetic_island.py wii/test_bulk_writer.py generated/gticlub2/gl_000.c
	@mkdir -p $(OUT)
	python3 wii/test_arithmetic_island.py --emit-native $@
$(OUT)/arithmetic_island_probe.o: $(OUT)/arithmetic_island_probe.c runtime/ppc_rt.h
	$(CC) $(CFLAGS) -Iwii -Itests/fixtures -fno-strict-aliasing -frounding-math -ffp-contract=off -c $< -o $@
$(OUT)/arithmetic_island_probe.elf: $(OUT)/arithmetic_island_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/arithmetic_island_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/arithmetic_island_probe.dol: $(OUT)/arithmetic_island_probe.elf
	elf2dol $< $@

.PHONY: 

.PHONY: gpr-multiple-probe
.PHONY: cr-unpack-probe
cr-unpack-probe: $(OUT)/cr_unpack_probe.dol
$(OUT)/cr_unpack_probe.c: wii/test_cr_unpack.py wii/cr_unpack.h runtime/cpu.c wii/test_bulk_writer.py
	@mkdir -p $(OUT)
	python3 wii/test_cr_unpack.py --emit-native $@
$(OUT)/cr_unpack_probe.o: $(OUT)/cr_unpack_probe.c runtime/ppc_rt.h
	$(CC) $(CFLAGS) -Iwii -Itests/fixtures -fno-strict-aliasing -frounding-math -ffp-contract=off -c $< -o $@
$(OUT)/cr_unpack_probe.elf: $(OUT)/cr_unpack_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/cr_unpack_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/cr_unpack_probe.dol: $(OUT)/cr_unpack_probe.elf
	elf2dol $< $@

gpr-multiple-probe: $(OUT)/gpr_multiple_probe.dol
$(OUT)/gpr_multiple_probe.c: wii/test_gpr_multiple.py wii/gpr_multiple.h wii/test_bulk_writer.py
	@mkdir -p $(OUT)
	python3 wii/test_gpr_multiple.py --emit-native $@
$(OUT)/gpr_multiple_probe.o: $(OUT)/gpr_multiple_probe.c runtime/ppc_rt.h
	$(CC) $(CFLAGS) -Iwii -Itests/fixtures -fno-strict-aliasing -frounding-math -ffp-contract=off -c $< -o $@
$(OUT)/gpr_multiple_probe.elf: $(OUT)/gpr_multiple_probe.o
	$(CC) $(MACHDEP) -Wl,--gc-sections,-Map,$(OUT)/gpr_multiple_probe.map -L$(LIBOGC_LIB) $^ $(LIBS) -o $@
$(OUT)/gpr_multiple_probe.dol: $(OUT)/gpr_multiple_probe.elf
	elf2dol $< $@


.PHONY: 




