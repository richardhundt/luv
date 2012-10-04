all:
	git submodule update --init ./src/uv
	git submodule update --init ./src/zmq
	make -C ./src

clean:
	make -C ./src clean

.PHONY: all clean
