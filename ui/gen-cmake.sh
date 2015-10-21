SRCDIR=$HOME/src/xtree/weather/ui
BDIR=$HOME/b/weatherui

mkdir -p ${BDIR}x
cd ${BDIR}x
cmake -G Xcode -DCMAKE_TOOLCHAIN_FILE=$SRCDIR/macports.cmake $SRCDIR

mkdir -p ${BDIR}b
cd ${BDIR}b
cmake -DCMAKE_TOOLCHAIN_FILE=$SRCDIR/macports.cmake $SRCDIR
