MAKEFLAGS += --silent
FLAGS = \
	-D_DEFAULT_SOURCE \
	-D_POSIX_C_SOURCE \
	-ferror-limit=1 \
	-fsanitize=address \
	-fsanitize=bounds \
	-fsanitize=float-divide-by-zero \
	-fsanitize=implicit-conversion \
	-fsanitize=integer \
	-fsanitize=nullability \
	-fsanitize=undefined \
	-fshort-enums \
	-g \
	-lGL \
	-lglfw \
	-march=native \
	-O3 \
	-std=c99 \
	-Werror \
	-Weverything \
	-Wno-c2x-extensions \
	-Wno-covered-switch-default \
	-Wno-declaration-after-statement \
	-Wno-extra-semi-stmt \
	-Wno-padded \
	-Wno-unsafe-buffer-usage

.PHONY: all
all: bin/main

.PHONY: clean
clean:
	rm -rf bin/

.PHONY: run
run: all
	./bin/main

bin/main: src/main.c
	mkdir -p bin/
	clang-format -i -verbose src/*.glsl src/main.c
	mold -run clang $(FLAGS) -o bin/main src/main.c

.PHONY: profile
profile: all
	sudo sh -c "echo 1 > /proc/sys/kernel/perf_event_paranoid"
	sudo sh -c "echo 0 > /proc/sys/kernel/kptr_restrict"
	perf record --call-graph fp ./bin/main
	perf report
	rm perf.data*
