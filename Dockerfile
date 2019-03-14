FROM ubuntu:18.04

# Install Dependencies via apt and pip
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -qq \
	autoconf automake libtool make \
	gcc-7 g++-7 \
	git \
	libbz2-dev liblzma-dev libncurses5-dev \
	python3-dev python3-matplotlib python3-numpy python3-pandas python3-scipy python3-setuptools python3-sphinx \
	zlib1g zlib1g-dev \
	wget && \
 	update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-7 100 && \
	update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-7 100 && \
	export CXX="g++-7" && export CC="gcc-7"

# Install samtools
RUN 	wget https://github.com/samtools/samtools/releases/download/1.7/samtools-1.7.tar.bz2 && \
	tar -xf samtools-1.7.tar.bz2 && \
	cd samtools-1.7 && \
	./configure && \
	make -j4 && \
	make install && \
	cd ..

# Install Portcullis
RUN 	git clone https://github.com/maplesond/portcullis.git && \
	cd portcullis && \
	./build_boost.sh && \
	./autogen.sh && \
	./configure && \
	make -j4 V=1 && \
	make install

