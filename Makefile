all : ./src/uv.a
	make -C ./src

clean :
	make -C ./src clean
	make -C ./deps/uv clean

.src/uv.a :
	git submodule update --init ./deps/uv
	cp ./deps/uv/uv.a ./src/uv.a


.PHONY: all clean
