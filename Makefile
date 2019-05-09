CC=clang
CFLAGS=-Wall -Werror -g -fsanitize=address,undefined -std=c99 -pedantic

lzw:

test_encoding.lzw: lzw
	./lzw -e < lzw.c > $@

debug_encoding.lzw: lzw
	./lzw -e -g < lzw.c > $@

test_decoding.txt: test_encoding.lzw
	./lzw -d < $< > $@

debug_decoding.txt: test_encoding.lzw
	./lzw -d -g < $< > $@

clean:
	rm -f lzw *.o *~ test_encoding.lzw test_decoding.txt
