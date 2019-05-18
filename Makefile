CC=clang
CFLAGS=-Wall -Werror -g -fsanitize=address,undefined -std=c99 -pedantic
#CFLAGS=-Wall -Werror -g -std=c99 -pedantic

lzw_main: lzw.o

lzw_fuzz: CFLAGS=-Wall -Werror -g -fsanitize=address,undefined,fuzzer -std=c99 -pedantic
lzw_fuzz: lzw.o

test: lzw_main
	cat lzw.c | ./lzw_main -e -g 2> encode_log.txt | ./lzw_main -d -g 2> decode_log.txt | diff lzw.c -

test2: lzw_main
	cat test2.txt | ./lzw_main -e -g 2> encode_log.txt | ./lzw_main -d -g 2> decode_log.txt | diff test2.txt -
test1: lzw_main
	cat test1.txt | ./lzw_main -e -g 2> encode_log.txt | ./lzw_main -d -g 2> decode_log.txt | diff test1.txt -
kennedy: lzw_main
	cat tests/kennedy.xls | ./lzw_main -e -g -s 2> encode_log.txt | ./lzw_main -d -g -s 2> decode_log.txt | diff tests/kennedy.xls -

clean:
	rm -f lzw_main *.o *~ test_encoding.lzw test_decoding.txt
