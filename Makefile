CC=clang
CFLAGS=-Wall -Werror -g -fsanitize=address,undefined -std=c99 -pedantic
#CFLAGS=-Wall -Werror -g -std=c99 -pedantic

lzw:

test: lzw
	cat lzw.c | ./lzw -e -g 2> encode_log.txt | ./lzw -d -g 2> decode_log.txt | diff lzw.c -

test1: lzw
	cat test1.txt | ./lzw -e -g 2> encode_log.txt | ./lzw -d -g 2> decode_log.txt | diff test1.txt -
kennedy: lzw
	cat tests/kennedy.xls | ./lzw -e -g -s 2> encode_log.txt | ./lzw -d -g -s 2> decode_log.txt | diff tests/kennedy.xls -

clean:
	rm -f lzw *.o *~ test_encoding.lzw test_decoding.txt
