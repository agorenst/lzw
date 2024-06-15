CC=clang
# CFLAGS=-Wall -Werror -g -fsanitize=address,undefined -std=c99 -pedantic
CFLAGS=-Wall -Werror -g -fsanitize=address,undefined -flto -O3
#CFLAGS=-Wall -Werror -g -std=c99 -pedantic -O2
#CFLAGS=-Wall -Werror -g -std=c99 -pedantic -O3 -flto -fsanitize=address,undefined

lzw_main: lzw.o

lzw_run_test: lzw_test
	./lzw_test

lzw_test: lzw.o

lzw_fuzz: CFLAGS=-Wall -Werror -g -fsanitize=address,undefined,fuzzer -O3 -flto
lzw_fuzz: lzw.o

# 4212811795
lzw_fuzz_run: lzw_fuzz
	./lzw_fuzz -max_len=1000000 FUZZ_CORPUS

test2: lzw_main
	cat lzw_failure.c | ./lzw_main -e -g 2 2> encode_log.txt | ./lzw_main -d -g 2 2> decode_log.txt | diff lzw_failure.c -
test: lzw_main
	cat lzw.c | ./lzw_main -e -g 2 2> encode_log.txt | ./lzw_main -d -g 2 2> decode_log.txt | diff lzw.c -

perf_record: lzw_main
	cat ./../cache/tracer/itrace.out | /usr/lib/linux-tools/5.15.0-107-generic/perf record -c 1000 -g  ./lzw_main -e -m 1024 > /dev/null

clean:
	rm -f lzw_main *.o *~ test_encoding.lzw test_decoding.txt lzw_fuzz
