all:
	git submodule update --init ./src/uv
	git submodule update --init ./src/zmq
	make -C ./src

clean:
	make -C ./src clean

realclean:
	make -C ./src realclean

.PHONY: all clean realclean
