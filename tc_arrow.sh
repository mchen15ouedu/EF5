# source this to get the GCC 12.2.0 + Arrow 11 toolchain
source /usr/share/lmod/lmod/init/bash 2>/dev/null
module load Arrow/11.0.0-gfbf-2022b 2>/dev/null
export ARROW_ROOT=/opt/oscer/software/Arrow/11.0.0-gfbf-2022b
export CXX=/opt/oscer/software/GCCcore/12.2.0/bin/g++
export CC=/opt/oscer/software/GCCcore/12.2.0/bin/gcc
