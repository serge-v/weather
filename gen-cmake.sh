SRCDIR=$HOME/src/xtree/weather

mkdir -p ~/b/weatherx
cd ~/b/weatherx
cmake -G Xcode -DCMAKE_TOOLCHAIN_FILE=$SRCDIR/macports.cmake $SRCDIR

mkdir -p ~/b/weatherb
cd ~/b/weatherb
cmake -DCMAKE_TOOLCHAIN_FILE=$SRCDIR/macports.cmake $SRCDIR
