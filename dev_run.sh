set -e
set -v

## MAKE

make XCXXFLAGS="-fsanitize=address" -j 4

## RUN

LD_PRELOAD=libasan.so.2 ./logp "$@"
