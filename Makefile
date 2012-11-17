all:
	git submodule update --init ./src/uv
	mkdir -p ./ray
	make -C ./src

clean:
	make -C ./src clean
	rm -f ray/*.so

test:
	make -C ./test

realclean:
	make -C ./src realclean

.PHONY: all clean realclean test
