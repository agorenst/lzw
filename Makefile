CC=clang
CFLAGS=-Wall -Werror -g -O2 -flto

lzw_main: lzw_main.o lzw.o
	$(CC) $(CFLAGS) $^ -o $@

lzw_pgopt: CC=clang
lzw_pgopt: CFLAGS=-fprofile-instr-use=lzw.profdata -O2 -flto -Wall -Werror -DNDEBUG
lzw_pgopt: lzw.c lzw_main.c | lzw.profdata
	$(CC) $(CFLAGS) $^ -o $@

lzw.profdata: lzw_pogo
	cat ./../cache/tracer/itrace.out | head -c 100000000 | LLVM_PROFILE_FILE="lzw-%m.profraw" ./lzw_pogo -c -m 4096 -x > /dev/null
	llvm-profdata-14 merge -output=lzw.profdata lzw-*.profraw
	rm *.profraw

lzw_pogo: CC=clang
lzw_pogo: CFLAGS=-fprofile-generate -O2 -flto -Wall -Werror -DNDEBUG
lzw_pogo: lzw.c lzw_main.c
	$(CC) $(CFLAGS) $^ -o $@

lzw_fuzz_run: lzw_fuzz
	mkdir -p ./FUZZ_CORPUS/LLVM/
	mkdir -p ./FUZZ_RESULT/LLVM/
	./lzw_fuzz -max_len=1000000 -len_control=1 -artifact_prefix=./FUZZ_RESULT/LLVM/ ./FUZZ_CORPUS/LLVM/

lzw_fuzz: CFLAGS=-Wall -Werror -g -fsanitize=address,undefined,fuzzer -DFUZZ_MODE -O2 -flto
lzw_fuzz: lzw_main.o lzw.o
	$(CC) $(CFLAGS) $^ -o $@

lzw_afl_fuzz: lzw_afl
	mkdir -p ./FUZZ_RESULT/AFL/
	afl-fuzz -i ./FUZZ_CORPUS/AFL_MIN -o ./FUZZ_RESULT/AFL -- ./lzw_afl -p 7 -x -C -m 513

min_fuzz_corpus: lzw_afl
	afl-cmin -i ./FUZZ_CORPUS/LLVM -o ./FUZZ_CORPUS/AFL_MIN -- ./lzw_afl -p 7 -x -C -m 513

lzw_afl: CC=afl-clang-fast
lzw_afl: CFLAGS=-Wall -Werror -g -fsanitize=address,undefined -O2 -flto
lzw_afl: lzw.c lzw_main.c
	$(CC) $(CFLAGS) $^ -o $@

lzw_run_test: lzw_test
	./lzw_test

lzw_test: lzw.o

test: lzw_main
	cat lzw.c | ./lzw_main -e -g kb 2> encode_log.txt | ./lzw_main -d -g kb 2> decode_log.txt | diff lzw.c -

perf_record: lzw_main
	cat ./../cache/tracer/itrace.out | head -c 248231103 | /usr/lib/linux-tools/5.15.0-113-generic/perf record -c 100 -g  ./lzw_main -c -m 1024 -x > /dev/null
# cat ./../cache/tracer/itrace.out | /usr/lib/linux-tools/5.15.0-113-generic/perf record -c 100 -g  ./lzw_main -e -m 1024 > /dev/null

clean_pgo:
	rm -f lzw_pogo lzw_pgopt *.profraw *profdata

clean: clean_pgo
	rm -f lzw_main *.o *~ *.dat *.lzw
	rm -f lzw_afl lzw_fuzz lzw lzw_test
