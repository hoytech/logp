set -e
set -v

## MAKE

make XCXXFLAGS="-fsanitize=address" -j 4

## RUN

LD_PRELOAD=`locate libasan.so | head -1` ./logp "$@"
