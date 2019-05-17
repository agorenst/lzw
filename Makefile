CC=clang
#CFLAGS=-Wall -Werror -g -fsanitize=address,undefined -std=c99 -pedantic
CFLAGS=-Wall -Werror -g -std=c99 -pedantic

lzw:

test: lzw
	cat lzw.c | ./lzw -e -g 2> encode_log.txt | ./lzw -d -g 2>decode_log.txt | diff lzw.c -

debugfiles: debug_encoding.lzw debug_decoding.txt
	diff debug_encoding.lzw debug_decoding.txt | more

test_encoding.lzw: lzw
	./lzw -e < lzw.c > $@

debug_encoding.lzw: lzw
	./lzw -e -g < lzw.c > $@ 2>&1

test_decoding.txt: test_encoding.lzw
	./lzw -d < $< > $@

debug_decoding.txt: test_encoding.lzw
	./lzw -d -g < $< > $@ 2>&1

clean:
	rm -f lzw *.o *~ test_encoding.lzw test_decoding.txt
